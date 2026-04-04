#ifndef OSHAL_PWM_H_
#define OSHAL_PWM_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Physical PWM outputs currently exposed by OSHAL.
/// @note The API remains timer-agnostic even when a specific board routes an
///     output through one concrete timer waveform pin.
typedef enum oshal_pwm_output_id {
	/// @brief SAMD21 PA8 routed as TCC0/WO[0].
	OSHAL_PWM_OUTPUT_PA8_TCC0_WO0 = 0,
	/// @brief Number of OSHAL PWM outputs currently exposed.
	OSHAL_PWM_OUTPUT_COUNT,
} oshal_pwm_output_id_t;

/// @brief Report whether an OSHAL PWM output is ready to use.
/// @param output_id Physical PWM output identifier to query.
/// @return True when the backend for the requested output is ready, otherwise
///     false.
bool oshal_pwm_output_is_ready(oshal_pwm_output_id_t output_id);

/// @brief Configure a PWM output with a period and pulse width.
/// @param output_id Physical PWM output identifier to configure.
/// @param period_ns Requested PWM period in nanoseconds.
/// @param pulse_ns Requested active pulse width in nanoseconds.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_pwm_output_configure(oshal_pwm_output_id_t output_id, uint32_t period_ns, uint32_t pulse_ns);

/// @brief Update only the active pulse width for a configured PWM output.
/// @param output_id Physical PWM output identifier to update.
/// @param pulse_ns Requested active pulse width in nanoseconds.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_pwm_output_set_pulse(oshal_pwm_output_id_t output_id, uint32_t pulse_ns);

/// @brief Enable waveform generation on a configured PWM output.
/// @param output_id Physical PWM output identifier to enable.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_pwm_output_enable(oshal_pwm_output_id_t output_id);

/// @brief Disable waveform generation on a configured PWM output.
/// @param output_id Physical PWM output identifier to disable.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_pwm_output_disable(oshal_pwm_output_id_t output_id);

/// @brief Return a human-readable name for an OSHAL PWM output.
/// @param output_id Physical PWM output identifier to describe.
/// @return Pointer to a static string naming the output.
const char *oshal_pwm_output_name(oshal_pwm_output_id_t output_id);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_PWM_H_ */