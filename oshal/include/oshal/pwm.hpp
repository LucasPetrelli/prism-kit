#ifndef OSHAL_PWM_HPP_
#define OSHAL_PWM_HPP_

#include <cstdint>

namespace oshal {

/// @brief Generic PWM output interface exposed by OSHAL.
/// @note OSHAL owns physical timer output bindings while higher layers own the
///     logical meaning attached to a waveform.
class PwmOutput {
public:
	PwmOutput(const PwmOutput &) = delete;
	PwmOutput &operator=(const PwmOutput &) = delete;
	virtual ~PwmOutput() = default;

	/// @brief Return a human-readable physical output name.
	/// @return Pointer to a static string describing the output.
	virtual const char *name() const = 0;

	/// @brief Report whether the backend for this PWM output is ready.
	/// @return True when the backend is ready, otherwise false.
	virtual bool is_ready() const = 0;

	/// @brief Configure the PWM output period and pulse width.
	/// @param period_ns Requested PWM period in nanoseconds.
	/// @param pulse_ns Requested active pulse width in nanoseconds.
	/// @return STATUS_OK on success, or a negative project-defined status code on
	///     failure.
	virtual int configure(std::uint32_t period_ns, std::uint32_t pulse_ns) = 0;

	/// @brief Update only the active pulse width for a configured output.
	/// @param pulse_ns Requested active pulse width in nanoseconds.
	/// @return STATUS_OK on success, or a negative project-defined status code on
	///     failure.
	virtual int set_pulse(std::uint32_t pulse_ns) = 0;

	/// @brief Enable waveform generation on a configured PWM output.
	/// @return STATUS_OK on success, or a negative project-defined status code on
	///     failure.
	virtual int enable() = 0;

	/// @brief Disable waveform generation on a configured PWM output.
	/// @return STATUS_OK on success, or a negative project-defined status code on
	///     failure.
	virtual int disable() = 0;

protected:
	PwmOutput() = default;
};

/// @brief Public OSHAL reference to the physical SAMD21 PA8 TCC0/WO[0] PWM output.
extern PwmOutput &pa8_tcc0_wo0;

} // namespace oshal

#endif /* OSHAL_PWM_HPP_ */