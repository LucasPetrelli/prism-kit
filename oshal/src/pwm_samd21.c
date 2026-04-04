#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <soc.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "oshal/pwm.h"
#include "oshal/status.h"
#include "pwm_backend.h"

#define OSHAL_PWM_TCC0_NODE DT_NODELABEL(tcc0)
#define OSHAL_PWM_INPUT_CLOCK_HZ SOC_ATMEL_SAM0_GCLK0_FREQ_HZ
#define OSHAL_PWM_COUNTER_MAX_CYCLES ((1UL << 24U) - 1UL)
#define OSHAL_PWM_PORT_GROUP_INDEX 0U
#define OSHAL_PWM_PORT_PIN_INDEX 8U

/* Keep the timing choices in one table so period handling stays predictable. */
struct oshal_pwm_prescaler_option {
	uint16_t divisor;
	uint32_t ctrla_bits;
};

/* Cache the last applied settings so live updates reuse the same backend state. */
struct oshal_pwm_binding {
	const char *name;
	Tcc *regs;
	uint8_t channel_index;
	bool configured;
	bool enabled;
	uint16_t prescaler_divisor;
	uint32_t prescaler_bits;
	uint32_t period_ns;
	uint32_t period_cycles;
	uint32_t pulse_cycles;
};

static const struct oshal_pwm_prescaler_option oshal_pwm_prescaler_options[] = {
	{1U, TCC_CTRLA_PRESCALER_DIV1},
	{2U, TCC_CTRLA_PRESCALER_DIV2},
	{4U, TCC_CTRLA_PRESCALER_DIV4},
	{8U, TCC_CTRLA_PRESCALER_DIV8},
	{16U, TCC_CTRLA_PRESCALER_DIV16},
	{64U, TCC_CTRLA_PRESCALER_DIV64},
	{256U, TCC_CTRLA_PRESCALER_DIV256},
	{1024U, TCC_CTRLA_PRESCALER_DIV1024},
};

static struct oshal_pwm_binding oshal_pwm_bindings[] = {
	[OSHAL_PWM_OUTPUT_PA8_TCC0_WO0] = {
		.name = "PA8/TCC0_WO0",
		.regs = TCC0,
		.channel_index = 0U,
	},
};

static int oshal_pwm_backend_status = STATUS_ERR_NOT_READY;

/* Clock writes are serialized through GCLK before TCC0 can be touched safely. */
static void oshal_pwm_wait_for_clock_sync(void)
{
	while ((GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY) != 0U) {
	}
}

/* TCC register writes are buffered, so each step waits until the block catches up. */
static void oshal_pwm_wait_for_tcc_sync(Tcc *regs)
{
	while (regs->SYNCBUSY.reg != 0U) {
	}
}

static struct oshal_pwm_binding *oshal_pwm_lookup(oshal_pwm_output_id_t output_id)
{
	if ((output_id < 0) || (output_id >= OSHAL_PWM_OUTPUT_COUNT)) {
		return NULL;
	}

	return &oshal_pwm_bindings[output_id];
}

/* The overlay reserves PA8 for PWM; the backend applies the matching mux directly. */
static void oshal_pwm_configure_pa8_mux(void)
{
	PortGroup *const port_group = &PORT->Group[OSHAL_PWM_PORT_GROUP_INDEX];

	port_group->DIRCLR.reg = BIT(OSHAL_PWM_PORT_PIN_INDEX);
	port_group->PINCFG[OSHAL_PWM_PORT_PIN_INDEX].reg = PORT_PINCFG_PMUXEN;
	port_group->PMUX[OSHAL_PWM_PORT_PIN_INDEX / 2U].bit.PMUXE = MUX_PA08E_TCC0_WO0;
}

/* Pick the first prescaler that fits the requested period without clipping it. */
static int oshal_pwm_choose_prescaler(uint32_t period_ns, uint16_t *divisor, uint32_t *ctrla_bits,
	uint32_t *period_cycles)
{
	const uint64_t numerator = (uint64_t)OSHAL_PWM_INPUT_CLOCK_HZ * (uint64_t)period_ns;

	for (size_t index = 0; index < ARRAY_SIZE(oshal_pwm_prescaler_options); ++index) {
		const struct oshal_pwm_prescaler_option *const option = &oshal_pwm_prescaler_options[index];
		const uint64_t denominator = 1000000000ULL * (uint64_t)option->divisor;
		uint64_t cycles = (numerator + (denominator / 2ULL)) / denominator;

		if (cycles == 0ULL) {
			cycles = 1ULL;
		}

		if (cycles <= (uint64_t)OSHAL_PWM_COUNTER_MAX_CYCLES) {
			*divisor = option->divisor;
			*ctrla_bits = option->ctrla_bits;
			*period_cycles = (uint32_t)cycles;
			return STATUS_OK;
		}
	}

	return STATUS_ERR_INVALID_ARGUMENT;
}

/* The public API stays in nanoseconds while the backend runs in timer counts. */
static int oshal_pwm_ns_to_cycles(uint32_t duration_ns, uint16_t prescaler_divisor, uint32_t *cycles)
{
	const uint64_t denominator = 1000000000ULL * (uint64_t)prescaler_divisor;
	uint64_t scaled_cycles =
		(((uint64_t)OSHAL_PWM_INPUT_CLOCK_HZ * (uint64_t)duration_ns) + (denominator / 2ULL)) /
		denominator;

	if (scaled_cycles > (uint64_t)OSHAL_PWM_COUNTER_MAX_CYCLES) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	*cycles = (uint32_t)scaled_cycles;
	return STATUS_OK;
}

/* Apply the next waveform in a short, ordered sequence so enable state stays consistent. */
static int oshal_pwm_program_waveform(struct oshal_pwm_binding *binding, uint32_t period_cycles,
	uint32_t pulse_cycles)
{
	const bool was_enabled = binding->enabled;

	if (pulse_cycles > period_cycles) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (was_enabled) {
		binding->regs->CTRLA.bit.ENABLE = 0;
		oshal_pwm_wait_for_tcc_sync(binding->regs);
	}

	binding->regs->PER.reg = TCC_PER_PER(period_cycles);
	binding->regs->CC[binding->channel_index].reg = TCC_CC_CC(pulse_cycles);
	binding->regs->CTRLA.reg = binding->prescaler_bits;
	binding->regs->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
	oshal_pwm_wait_for_tcc_sync(binding->regs);

	if (was_enabled) {
		binding->regs->CTRLA.bit.ENABLE = 1;
		oshal_pwm_wait_for_tcc_sync(binding->regs);
	}

	binding->period_cycles = period_cycles;
	binding->pulse_cycles = pulse_cycles;
	binding->configured = true;
	return STATUS_OK;
}

int oshal_pwm_backend_init(void)
{
	struct oshal_pwm_binding *const binding = &oshal_pwm_bindings[OSHAL_PWM_OUTPUT_PA8_TCC0_WO0];

	if (oshal_pwm_backend_status == STATUS_OK) {
		return STATUS_OK;
	}

	/* Bring up clocks first, then route the pin, then reset TCC0 into a known state. */
	PM->APBCMASK.reg |= PM_APBCMASK_TCC0;
	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1;
	oshal_pwm_wait_for_clock_sync();
	oshal_pwm_configure_pa8_mux();

	binding->regs->CTRLA.bit.ENABLE = 0;
	oshal_pwm_wait_for_tcc_sync(binding->regs);
	binding->regs->CTRLA.bit.SWRST = 1;
	oshal_pwm_wait_for_tcc_sync(binding->regs);
	binding->regs->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;
	binding->regs->PER.reg = TCC_PER_PER(0U);
	binding->regs->CC[binding->channel_index].reg = TCC_CC_CC(0U);
	oshal_pwm_wait_for_tcc_sync(binding->regs);

	binding->configured = false;
	binding->enabled = false;
	binding->prescaler_divisor = 0U;
	binding->prescaler_bits = 0U;
	binding->period_ns = 0U;
	binding->period_cycles = 0U;
	binding->pulse_cycles = 0U;
	oshal_pwm_backend_status = STATUS_OK;
	return STATUS_OK;
}

bool oshal_pwm_output_is_ready(oshal_pwm_output_id_t output_id)
{
	return (oshal_pwm_lookup(output_id) != NULL) && (oshal_pwm_backend_status == STATUS_OK);
}

int oshal_pwm_output_configure(oshal_pwm_output_id_t output_id, uint32_t period_ns, uint32_t pulse_ns)
{
	struct oshal_pwm_binding *binding = oshal_pwm_lookup(output_id);
	uint32_t period_cycles;
	uint32_t pulse_cycles;
	int ret;

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Reject impossible requests early so the timer path stays simple and explicit. */
	if ((period_ns == 0U) || (pulse_ns > period_ns)) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Lazy init keeps startup narrow while still allowing direct API use later on. */
	if (oshal_pwm_backend_status != STATUS_OK) {
		ret = oshal_pwm_backend_init();
		if (ret < 0) {
			return ret;
		}
	}

	ret = oshal_pwm_choose_prescaler(period_ns, &binding->prescaler_divisor, &binding->prescaler_bits,
		&period_cycles);
	if (ret < 0) {
		return ret;
	}

	ret = oshal_pwm_ns_to_cycles(pulse_ns, binding->prescaler_divisor, &pulse_cycles);
	if (ret < 0) {
		return ret;
	}

	binding->period_ns = period_ns;
	return oshal_pwm_program_waveform(binding, period_cycles, pulse_cycles);
}

int oshal_pwm_output_set_pulse(oshal_pwm_output_id_t output_id, uint32_t pulse_ns)
{
	struct oshal_pwm_binding *binding = oshal_pwm_lookup(output_id);
	uint32_t pulse_cycles;
	int ret;

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!binding->configured) {
		return STATUS_ERR_NOT_READY;
	}

	if (pulse_ns > binding->period_ns) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Reuse the last period so duty changes do not force callers to restate timing. */
	ret = oshal_pwm_ns_to_cycles(pulse_ns, binding->prescaler_divisor, &pulse_cycles);
	if (ret < 0) {
		return ret;
	}

	return oshal_pwm_program_waveform(binding, binding->period_cycles, pulse_cycles);
}

int oshal_pwm_output_enable(oshal_pwm_output_id_t output_id)
{
	struct oshal_pwm_binding *binding = oshal_pwm_lookup(output_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!binding->configured) {
		return STATUS_ERR_NOT_READY;
	}

	if (binding->enabled) {
		return STATUS_OK;
	}

	/* Enable is separate from configure so bring-up code can stage the waveform first. */
	binding->regs->CTRLA.bit.ENABLE = 1;
	oshal_pwm_wait_for_tcc_sync(binding->regs);
	binding->enabled = true;
	return STATUS_OK;
}

int oshal_pwm_output_disable(oshal_pwm_output_id_t output_id)
{
	struct oshal_pwm_binding *binding = oshal_pwm_lookup(output_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!binding->configured) {
		return STATUS_ERR_NOT_READY;
	}

	if (!binding->enabled) {
		return STATUS_OK;
	}

	/* Disable preserves the cached configuration so the same output can resume cleanly. */
	binding->regs->CTRLA.bit.ENABLE = 0;
	oshal_pwm_wait_for_tcc_sync(binding->regs);
	binding->enabled = false;
	return STATUS_OK;
}

const char *oshal_pwm_output_name(oshal_pwm_output_id_t output_id)
{
	const struct oshal_pwm_binding *binding = oshal_pwm_lookup(output_id);

	if (binding == NULL) {
		return "invalid_pwm_output";
	}

	return binding->name;
}