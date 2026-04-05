#include <cstddef>
#include <cstdint>
#include <iterator>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <soc.h>

#include "oshal/status.h"
#include "pwm_samd21_internal.hpp"

namespace {

/*
 * The SAMD21 routes TCC0 from the main generic clock in this bring-up, so all
 * period and pulse calculations are derived from the GCLK0 rate that Zephyr's
 * SoC support publishes.
 */
constexpr std::uint32_t kPwmInputClockHz = SOC_ATMEL_SAM0_GCLK0_FREQ_HZ;

/* TCC0 is 24-bit wide on SAMD21, so any computed period must fit this range. */
constexpr std::uint32_t kPwmCounterMaxCycles = (1UL << 24U) - 1UL;

/* Keep the supported prescaler ladder in one place so period fitting stays deterministic. */
struct Samd21PwmPrescalerOption {
	std::uint16_t divisor;
	std::uint32_t ctrla_bits;
};

constexpr Samd21PwmPrescalerOption kSamd21PwmPrescalerOptions[] = {
	{1U, TCC_CTRLA_PRESCALER_DIV1},
	{2U, TCC_CTRLA_PRESCALER_DIV2},
	{4U, TCC_CTRLA_PRESCALER_DIV4},
	{8U, TCC_CTRLA_PRESCALER_DIV8},
	{16U, TCC_CTRLA_PRESCALER_DIV16},
	{64U, TCC_CTRLA_PRESCALER_DIV64},
	{256U, TCC_CTRLA_PRESCALER_DIV256},
	{1024U, TCC_CTRLA_PRESCALER_DIV1024},
};

} // namespace

namespace oshal::internal {

Samd21PwmOutput::Samd21PwmOutput(const char *pwm_name, Tcc *regs, std::uint8_t channel_index,
	std::uint8_t port_group_index, std::uint8_t port_pin_index, std::uint8_t pin_mux_value)
	: name_(pwm_name),
	  regs_(regs),
	  channel_index_(channel_index),
	  port_group_index_(port_group_index),
	  port_pin_index_(port_pin_index),
	  pin_mux_value_(pin_mux_value),
	  backend_status_(STATUS_ERR_NOT_READY),
	  configured_(false),
	  enabled_(false),
	  prescaler_divisor_(0U),
	  prescaler_bits_(0U),
	  period_ns_(0U),
	  period_cycles_(0U),
	  pulse_cycles_(0U)
{
}

const char *Samd21PwmOutput::name() const
{
	return name_;
}

bool Samd21PwmOutput::is_ready() const
{
	return backend_status_ == STATUS_OK;
}

/* GCLK writes are synchronized through hardware, so polling is required before touching TCC. */
void Samd21PwmOutput::wait_for_clock_sync()
{
	while ((GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) != 0U) {
	}
}

/* TCC register updates are also buffered, so ordered configuration depends on this fence. */
void Samd21PwmOutput::wait_for_tcc_sync(Tcc *regs)
{
	while (regs->SYNCBUSY.reg != 0U) {
	}
}

/*
 * The board overlay reserves PA8 for this waveform path, but the mux still has
 * to be applied explicitly because this backend programs the timer registers
 * directly instead of going through Zephyr's PWM driver layer.
 */
void Samd21PwmOutput::configure_pin_mux() const
{
	PortGroup *const port_group = &PORT->Group[port_group_index_];

	port_group->DIRCLR.reg = BIT(port_pin_index_);
	port_group->PINCFG[port_pin_index_].reg = PORT_PINCFG_PMUXEN;

	if ((port_pin_index_ & 1U) == 0U) {
		port_group->PMUX[port_pin_index_ / 2U].bit.PMUXE = pin_mux_value_;
	} else {
		port_group->PMUX[port_pin_index_ / 2U].bit.PMUXO = pin_mux_value_;
	}
}

int Samd21PwmOutput::choose_prescaler(std::uint32_t period_ns, std::uint16_t *divisor,
	std::uint32_t *ctrla_bits, std::uint32_t *period_cycles)
{
	/* Convert the requested period into timer counts and pick the first prescaler that fits. */
	const std::uint64_t numerator = static_cast<std::uint64_t>(kPwmInputClockHz) *
		static_cast<std::uint64_t>(period_ns);

	for (std::size_t index = 0; index < std::size(kSamd21PwmPrescalerOptions);
		 ++index) {
		const Samd21PwmPrescalerOption &option = kSamd21PwmPrescalerOptions[index];
		const std::uint64_t denominator = 1000000000ULL * static_cast<std::uint64_t>(option.divisor);
		std::uint64_t cycles = (numerator + (denominator / 2ULL)) / denominator;

		if (cycles == 0ULL) {
			cycles = 1ULL;
		}

		if (cycles <= static_cast<std::uint64_t>(kPwmCounterMaxCycles)) {
			*divisor = option.divisor;
			*ctrla_bits = option.ctrla_bits;
			*period_cycles = static_cast<std::uint32_t>(cycles);
			return STATUS_OK;
		}
	}

	return STATUS_ERR_INVALID_ARGUMENT;
}

int Samd21PwmOutput::ns_to_cycles(std::uint32_t duration_ns, std::uint16_t prescaler_divisor,
	std::uint32_t *cycles)
{
	/* Keep the public API in nanoseconds while the hardware stays entirely count-based. */
	const std::uint64_t denominator = 1000000000ULL * static_cast<std::uint64_t>(prescaler_divisor);
	const std::uint64_t scaled_cycles =
		((static_cast<std::uint64_t>(kPwmInputClockHz) * static_cast<std::uint64_t>(duration_ns)) +
			(denominator / 2ULL)) /
		denominator;

	if (scaled_cycles > static_cast<std::uint64_t>(kPwmCounterMaxCycles)) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	*cycles = static_cast<std::uint32_t>(scaled_cycles);
	return STATUS_OK;
}

int Samd21PwmOutput::program_waveform(std::uint32_t period_cycles, std::uint32_t pulse_cycles)
{
	const bool was_enabled = enabled_;

	if (pulse_cycles > period_cycles) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Reprogram disabled hardware whenever possible so the output does not glitch mid-update. */
	if (was_enabled) {
		regs_->CTRLA.bit.ENABLE = 0;
		wait_for_tcc_sync(regs_);
	}

	/* PER defines the full period and CC[channel] defines the active pulse width. */
	regs_->PER.reg = TCC_PER_PER(period_cycles);
	regs_->CC[channel_index_].reg = TCC_CC_CC(pulse_cycles);
	regs_->CTRLA.reg = (regs_->CTRLA.reg & ~TCC_CTRLA_PRESCALER_Msk) | prescaler_bits_;
	regs_->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
	wait_for_tcc_sync(regs_);

	/* Restore the previous enable state so configure/set_pulse preserve caller intent. */
	if (was_enabled) {
		regs_->CTRLA.bit.ENABLE = 1;
		wait_for_tcc_sync(regs_);
	}

	/* Cache the applied counts so later duty-cycle updates can reuse the active period. */
	period_cycles_ = period_cycles;
	pulse_cycles_ = pulse_cycles;
	configured_ = true;
	return STATUS_OK;
}

int Samd21PwmOutput::initialize()
{
	if (backend_status_ == STATUS_OK) {
		return STATUS_OK;
	}

	/* Bring up clocks first, then route the pin, then reset TCC0 into a known state. */
	PM->APBCMASK.reg |= PM_APBCMASK_TCC0;
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1;
	wait_for_clock_sync();
	configure_pin_mux();

	regs_->CTRLA.bit.ENABLE = 0;
	wait_for_tcc_sync(regs_);
	regs_->CTRLA.bit.SWRST = 1;
	wait_for_tcc_sync(regs_);
	regs_->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
	regs_->PER.reg = TCC_PER_PER(0U);
	regs_->CC[channel_index_].reg = TCC_CC_CC(0U);
	wait_for_tcc_sync(regs_);

	/* Clear all cached state so the first configure call starts from a known software model too. */
	configured_ = false;
	enabled_ = false;
	prescaler_divisor_ = 0U;
	prescaler_bits_ = 0U;
	period_ns_ = 0U;
	period_cycles_ = 0U;
	pulse_cycles_ = 0U;
	backend_status_ = STATUS_OK;
	return STATUS_OK;
}

int Samd21PwmOutput::configure(std::uint32_t period_ns, std::uint32_t pulse_ns)
{
	std::uint32_t period_cycles;
	std::uint32_t pulse_cycles;
	int ret;

	/* Reject impossible requests early so the register programming path stays simple. */
	if ((period_ns == 0U) || (pulse_ns > period_ns)) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Lazy init keeps startup narrow while still allowing direct PWM use later on. */
	if (backend_status_ != STATUS_OK) {
		ret = initialize();
		if (ret < 0) {
			return ret;
		}
	}

	ret = choose_prescaler(period_ns, &prescaler_divisor_, &prescaler_bits_, &period_cycles);
	if (ret < 0) {
		return ret;
	}

	ret = ns_to_cycles(pulse_ns, prescaler_divisor_, &pulse_cycles);
	if (ret < 0) {
		return ret;
	}

	/* Preserve the requested period in nanoseconds for future set_pulse validation. */
	period_ns_ = period_ns;
	return program_waveform(period_cycles, pulse_cycles);
}

int Samd21PwmOutput::set_pulse(std::uint32_t pulse_ns)
{
	std::uint32_t pulse_cycles;
	int ret;

	/* Duty-cycle updates only make sense after a full configure established the period. */
	if (!configured_) {
		return STATUS_ERR_NOT_READY;
	}

	if (pulse_ns > period_ns_) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	ret = ns_to_cycles(pulse_ns, prescaler_divisor_, &pulse_cycles);
	if (ret < 0) {
		return ret;
	}

	/* Reuse the last programmed period so callers only restate the changing quantity. */
	return program_waveform(period_cycles_, pulse_cycles);
}

int Samd21PwmOutput::enable()
{
	/* Separating enable from configure lets bring-up stage a waveform before driving the pin. */
	if (!configured_) {
		return STATUS_ERR_NOT_READY;
	}

	if (enabled_) {
		return STATUS_OK;
	}

	regs_->CTRLA.bit.ENABLE = 1;
	wait_for_tcc_sync(regs_);
	enabled_ = true;
	return STATUS_OK;
}

int Samd21PwmOutput::disable()
{
	/* Disable preserves cached configuration so the same waveform can resume cleanly. */
	if (!configured_) {
		return STATUS_ERR_NOT_READY;
	}

	if (!enabled_) {
		return STATUS_OK;
	}

	regs_->CTRLA.bit.ENABLE = 0;
	wait_for_tcc_sync(regs_);
	enabled_ = false;
	return STATUS_OK;
}

} // namespace oshal::internal