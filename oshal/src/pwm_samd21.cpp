#include <soc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

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

/* Keep the supported prescaler ladder in one place so period fitting stays
 * deterministic. */
struct Samd21PwmPrescalerOption {
  std::uint16_t divisor;
  std::uint32_t ctrla_bits;
};

constexpr Samd21PwmPrescalerOption kSamd21PwmPrescalerOptions[] = {
  {1U, TCC_CTRLA_PRESCALER_DIV1},     {2U, TCC_CTRLA_PRESCALER_DIV2},
  {4U, TCC_CTRLA_PRESCALER_DIV4},     {8U, TCC_CTRLA_PRESCALER_DIV8},
  {16U, TCC_CTRLA_PRESCALER_DIV16},   {64U, TCC_CTRLA_PRESCALER_DIV64},
  {256U, TCC_CTRLA_PRESCALER_DIV256}, {1024U, TCC_CTRLA_PRESCALER_DIV1024},
};

/* Sentinel for unresolved DMAC trigger routing. */
constexpr std::uint8_t kInvalidDmaTriggerSource = 0xFFU;

}  // namespace

namespace oshal::internal {

Samd21PwmOutput::Samd21PwmOutput(const char* pwm_name, Tcc* regs,
                                 std::uint8_t channel_index,
                                 std::uint8_t port_group_index,
                                 std::uint8_t port_pin_index,
                                 std::uint8_t pin_mux_value)
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
      pulse_cycles_(0U) {}

Samd21DmaPwmOutput::Samd21DmaPwmOutput(const char* pwm_name, Tcc* regs,
                                       std::uint8_t channel_index,
                                       std::uint8_t port_group_index,
                                       std::uint8_t port_pin_index,
                                       std::uint8_t pin_mux_value,
                                       std::uint8_t dmac_channel_index)
    : Samd21PwmOutput(pwm_name, regs, channel_index, port_group_index,
                      port_pin_index, pin_mux_value),
      dma_trigger_source_(kInvalidDmaTriggerSource),
      dmac_channel_(dmac_channel_index),
      dma_pulse_cycles_{},
      dma_pulse_count_(0U),
      dma_remaining_repeats_(0U),
      dma_repeat_forever_(false),
      dma_sequence_active_(false),
      dma_sequence_error_(false) {}

const char* Samd21PwmOutput::name() const { return name_; }

bool Samd21PwmOutput::is_ready() const { return backend_status_ == STATUS_OK; }

/* GCLK writes are synchronized through hardware, so polling is required before
 * touching TCC. */
void Samd21PwmOutput::wait_for_clock_sync() {
  while ((GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) != 0U) {
  }
}

/* TCC register updates are also buffered, so ordered configuration depends on
 * this fence. */
void Samd21PwmOutput::wait_for_tcc_sync(Tcc* regs) {
  while (regs->SYNCBUSY.reg != 0U) {
  }
}

/*
 * The board overlay reserves PA8 for this waveform path, but the mux still has
 * to be applied explicitly because this backend programs the timer registers
 * directly instead of going through Zephyr's PWM driver layer.
 */
void Samd21PwmOutput::configure_pin_mux() const {
  PortGroup* const port_group = &PORT->Group[port_group_index_];

  port_group->DIRCLR.reg = BIT(port_pin_index_);
  port_group->PINCFG[port_pin_index_].reg = PORT_PINCFG_PMUXEN;

  if ((port_pin_index_ & 1U) == 0U) {
    port_group->PMUX[port_pin_index_ / 2U].bit.PMUXE = pin_mux_value_;
  } else {
    port_group->PMUX[port_pin_index_ / 2U].bit.PMUXO = pin_mux_value_;
  }
}

int Samd21PwmOutput::choose_prescaler(std::uint32_t period_ns,
                                      std::uint16_t* divisor,
                                      std::uint32_t* ctrla_bits,
                                      std::uint32_t* period_cycles) {
  /* Convert the requested period into timer counts and pick the first prescaler
   * that fits. */
  const std::uint64_t numerator = static_cast<std::uint64_t>(kPwmInputClockHz) *
                                  static_cast<std::uint64_t>(period_ns);

  for (std::size_t index = 0; index < std::size(kSamd21PwmPrescalerOptions);
       ++index) {
    const Samd21PwmPrescalerOption& option = kSamd21PwmPrescalerOptions[index];
    const std::uint64_t denominator =
      1000000000ULL * static_cast<std::uint64_t>(option.divisor);
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

int Samd21PwmOutput::ns_to_cycles(std::uint32_t duration_ns,
                                  std::uint16_t prescaler_divisor,
                                  std::uint32_t* cycles) {
  /* Keep the public API in nanoseconds while the hardware stays entirely
   * count-based. */
  const std::uint64_t denominator =
    1000000000ULL * static_cast<std::uint64_t>(prescaler_divisor);
  const std::uint64_t scaled_cycles =
    ((static_cast<std::uint64_t>(kPwmInputClockHz) *
      static_cast<std::uint64_t>(duration_ns)) +
     (denominator / 2ULL)) /
    denominator;

  if (scaled_cycles > static_cast<std::uint64_t>(kPwmCounterMaxCycles)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  *cycles = static_cast<std::uint32_t>(scaled_cycles);
  return STATUS_OK;
}

int Samd21PwmOutput::program_waveform(std::uint32_t period_cycles,
                                      std::uint32_t pulse_cycles) {
  const bool was_enabled = enabled_;

  if (pulse_cycles > period_cycles) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  /* Reprogram disabled hardware whenever possible so the output does not glitch
   * mid-update. */
  if (was_enabled) {
    regs_->CTRLA.bit.ENABLE = 0;
    wait_for_tcc_sync(regs_);
  }

  /* PER defines the full period and CC[channel] defines the active pulse width.
   */
  regs_->PER.reg = TCC_PER_PER(period_cycles);
  regs_->CC[channel_index_].reg = TCC_CC_CC(pulse_cycles);
  regs_->CTRLA.reg =
    (regs_->CTRLA.reg & ~TCC_CTRLA_PRESCALER_Msk) | prescaler_bits_;
  regs_->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
  wait_for_tcc_sync(regs_);

  /* Restore the previous enable state so configure/set_pulse preserve caller
   * intent. */
  if (was_enabled) {
    regs_->CTRLA.bit.ENABLE = 1;
    wait_for_tcc_sync(regs_);
  }

  /* Cache the applied counts so later duty-cycle updates can reuse the active
   * period. */
  period_cycles_ = period_cycles;
  pulse_cycles_ = pulse_cycles;
  configured_ = true;
  return STATUS_OK;
}

int Samd21PwmOutput::stop_transient_activity() { return STATUS_OK; }

void Samd21PwmOutput::reset_waveform_state() {
  configured_ = false;
  enabled_ = false;
  prescaler_divisor_ = 0U;
  prescaler_bits_ = 0U;
  period_ns_ = 0U;
  period_cycles_ = 0U;
  pulse_cycles_ = 0U;
}

int Samd21DmaPwmOutput::resolve_dma_trigger_source(
  const Tcc* regs, std::uint8_t channel_index, std::uint8_t* trigger_source) {
  if (trigger_source == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if (regs == TCC0) {
    *trigger_source = TCC0_DMAC_ID_OVF;
    return STATUS_OK;
  }

  if (regs == TCC1) {
    *trigger_source = TCC1_DMAC_ID_OVF;
    return STATUS_OK;
  }

  if (regs == TCC2) {
    *trigger_source = TCC2_DMAC_ID_OVF;
    return STATUS_OK;
  }

  (void)channel_index;
  return STATUS_ERR_INVALID_ARGUMENT;
}

void Samd21DmaPwmOutput::on_dma_sequence_event(void* context,
                                               bool transfer_error) {
  auto* self = static_cast<Samd21DmaPwmOutput*>(context);

  if (self != nullptr) {
    self->handle_dma_sequence_event(transfer_error);
  }
}

void Samd21DmaPwmOutput::handle_dma_sequence_event(bool transfer_error) {
  int ret;

  if (transfer_error) {
    dma_sequence_error_ = true;
    dma_sequence_active_ = false;
    (void)dmac_channel_.stop();
    return;
  }

  if (!dma_sequence_active_ || dma_repeat_forever_) {
    return;
  }

  if (dma_remaining_repeats_ == 0U) {
    dma_sequence_active_ = false;
    return;
  }

  ret = dmac_channel_.start();
  if (ret < 0) {
    dma_sequence_error_ = true;
    dma_sequence_active_ = false;
    return;
  }

  --dma_remaining_repeats_;
}

int Samd21DmaPwmOutput::build_dma_sequence(
  const std::uint32_t* pulse_ns_sequence, std::size_t pulse_count) {
  int ret;

  if ((pulse_ns_sequence == nullptr) || (pulse_count == 0U) ||
      (pulse_count > kMaxDmaPulseCount)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t index = 0; index < pulse_count; ++index) {
    std::uint32_t pulse_cycles;

    if (pulse_ns_sequence[index] > period_ns_) {
      return STATUS_ERR_INVALID_ARGUMENT;
    }

    ret =
      ns_to_cycles(pulse_ns_sequence[index], prescaler_divisor_, &pulse_cycles);
    if (ret < 0) {
      return ret;
    }

    if (pulse_cycles > period_cycles_) {
      return STATUS_ERR_INVALID_ARGUMENT;
    }

    /* DMAC source addressing decrements per beat, so stage values in reverse
     * order. */
    dma_pulse_cycles_[pulse_count - 1U - index] = pulse_cycles;
  }

  dma_pulse_count_ = pulse_count;
  return STATUS_OK;
}

void Samd21DmaPwmOutput::reset_sequence_state() {
  dma_pulse_count_ = 0U;
  dma_remaining_repeats_ = 0U;
  dma_repeat_forever_ = false;
  dma_sequence_active_ = false;
  dma_sequence_error_ = false;
}

int Samd21PwmOutput::initialize() {
  if (backend_status_ == STATUS_OK) {
    return STATUS_OK;
  }

  /* Bring up clocks first, then route the pin, then reset TCC0 into a known
   * state. */
  PM->APBCMASK.reg |= PM_APBCMASK_TCC0;
  GCLK->CLKCTRL.reg =
    GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1;
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

  /* Clear all cached state so the first configure call starts from a known
   * software model too. */
  reset_waveform_state();
  backend_status_ = STATUS_OK;
  return STATUS_OK;
}

int Samd21DmaPwmOutput::initialize() {
  int ret;

  ret = Samd21PwmOutput::initialize();
  if (ret < 0) {
    return ret;
  }

  if (dma_trigger_source_ == kInvalidDmaTriggerSource) {
    ret =
      resolve_dma_trigger_source(regs_, channel_index_, &dma_trigger_source_);
    if (ret < 0) {
      return ret;
    }
  }

  reset_sequence_state();
  return STATUS_OK;
}

int Samd21PwmOutput::configure(std::uint32_t period_ns,
                               std::uint32_t pulse_ns) {
  std::uint32_t period_cycles;
  std::uint32_t pulse_cycles;
  int ret;

  /* Reject impossible requests early so the register programming path stays
   * simple. */
  if ((period_ns == 0U) || (pulse_ns > period_ns)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  ret = stop_transient_activity();
  if (ret < 0) {
    return ret;
  }

  /* Lazy init keeps startup narrow while still allowing direct PWM use later
   * on. */
  if (backend_status_ != STATUS_OK) {
    ret = initialize();
    if (ret < 0) {
      return ret;
    }
  }

  ret = choose_prescaler(period_ns, &prescaler_divisor_, &prescaler_bits_,
                         &period_cycles);
  if (ret < 0) {
    return ret;
  }

  ret = ns_to_cycles(pulse_ns, prescaler_divisor_, &pulse_cycles);
  if (ret < 0) {
    return ret;
  }

  /* Preserve the requested period in nanoseconds for future set_pulse
   * validation. */
  period_ns_ = period_ns;
  return program_waveform(period_cycles, pulse_cycles);
}

int Samd21PwmOutput::set_pulse(std::uint32_t pulse_ns) {
  std::uint32_t pulse_cycles;
  int ret;

  /* Duty-cycle updates only make sense after a full configure established the
   * period. */
  if (!configured_) {
    return STATUS_ERR_NOT_READY;
  }

  ret = stop_transient_activity();
  if (ret < 0) {
    return ret;
  }

  if (pulse_ns > period_ns_) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  ret = ns_to_cycles(pulse_ns, prescaler_divisor_, &pulse_cycles);
  if (ret < 0) {
    return ret;
  }

  /* Reuse the last programmed period so callers only restate the changing
   * quantity. */
  return program_waveform(period_cycles_, pulse_cycles);
}

int Samd21PwmOutput::enable() {
  /* Separating enable from configure lets bring-up stage a waveform before
   * driving the pin. */
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

int Samd21PwmOutput::disable() {
  /* Disable preserves cached configuration so the same waveform can resume
   * cleanly. */
  if (!configured_) {
    return STATUS_ERR_NOT_READY;
  }

  if (!enabled_) {
    return STATUS_OK;
  }

  int ret = stop_transient_activity();
  if (ret < 0) {
    return ret;
  }

  regs_->CTRLA.bit.ENABLE = 0;
  wait_for_tcc_sync(regs_);
  enabled_ = false;
  return STATUS_OK;
}

int Samd21DmaPwmOutput::play_pulse_sequence(
  const std::uint32_t* pulse_ns_sequence, std::size_t pulse_count,
  std::uint32_t repeat_count) {
  int ret;

  /* Sequence playback depends on a ready backend plus resolved DMAC trigger
   * routing. */
  if (backend_status_ != STATUS_OK) {
    ret = initialize();
    if (ret < 0) {
      return ret;
    }
  }

  if (!configured_) {
    return STATUS_ERR_NOT_READY;
  }

  if (dma_trigger_source_ == kInvalidDmaTriggerSource) {
    return STATUS_ERR_NOT_READY;
  }

  ret = build_dma_sequence(pulse_ns_sequence, pulse_count);
  if (ret < 0) {
    return ret;
  }

  ret = stop_pulse_sequence();
  if (ret < 0) {
    return ret;
  }

  dma_repeat_forever_ = (repeat_count == 0U);
  dma_remaining_repeats_ = dma_repeat_forever_ ? 0U : repeat_count;
  dma_sequence_error_ = false;

  ret = dmac_channel_.configure_word_stream(
    &regs_->CC[channel_index_].reg, dma_pulse_cycles_, dma_pulse_count_,
    dma_trigger_source_, dma_repeat_forever_,
    &Samd21DmaPwmOutput::on_dma_sequence_event, this);
  if (ret < 0) {
    return ret;
  }

  if (!enabled_) {
    ret = enable();
    if (ret < 0) {
      return ret;
    }
  }

  ret = dmac_channel_.start();
  if (ret < 0) {
    return ret;
  }

  dma_sequence_active_ = true;
  if (!dma_repeat_forever_ && (dma_remaining_repeats_ > 0U)) {
    --dma_remaining_repeats_;
  }
  return STATUS_OK;
}

int Samd21DmaPwmOutput::stop_pulse_sequence() {
  (void)dmac_channel_.stop();
  dma_sequence_active_ = false;
  dma_repeat_forever_ = false;
  dma_remaining_repeats_ = 0U;
  return STATUS_OK;
}

bool Samd21DmaPwmOutput::is_pulse_sequence_active() const {
  return dma_sequence_active_ && !dma_sequence_error_;
}

int Samd21DmaPwmOutput::stop_transient_activity() {
  return stop_pulse_sequence();
}

}  // namespace oshal::internal