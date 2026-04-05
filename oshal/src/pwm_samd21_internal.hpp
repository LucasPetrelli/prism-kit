#ifndef OSHAL_PWM_SAMD21_INTERNAL_HPP_
#define OSHAL_PWM_SAMD21_INTERNAL_HPP_

#include <cstdint>

#include <soc.h>

#include "oshal/pwm.hpp"

namespace oshal::internal {

/// @brief SAMD21 TCC-backed PWM implementation for a fixed waveform output.
class Samd21PwmOutput final : public PwmOutput {
public:
	/// @brief Construct a SAMD21 PWM object.
	/// @param pwm_name Static diagnostic name for the PWM output.
	/// @param regs TCC register block that drives the output.
	/// @param channel_index Compare channel index used by the output.
	/// @param port_group_index PORT group index that owns the physical pin.
	/// @param port_pin_index Physical pin index inside the PORT group.
	/// @param pin_mux_value Peripheral mux value for the waveform route.
	Samd21PwmOutput(const char *pwm_name, Tcc *regs, std::uint8_t channel_index,
		std::uint8_t port_group_index, std::uint8_t port_pin_index, std::uint8_t pin_mux_value);

	const char *name() const override;
	bool is_ready() const override;
	int configure(std::uint32_t period_ns, std::uint32_t pulse_ns) override;
	int set_pulse(std::uint32_t pulse_ns) override;
	int enable() override;
	int disable() override;

	/// @brief Initialize the backend hardware for this PWM output.
	/// @return STATUS_OK on success, or a negative project-defined status code on
	///     failure.
	int initialize();

private:
	/// @brief Wait for synchronization of the device-global generic clock controller.
	/// @details On SAMD21, the GCLK peripheral is exposed through the MCU headers as
	///     a single global register block rather than a per-instance object, so this
	///     helper intentionally does not take a register pointer. This differs from
	///     wait_for_tcc_sync(), which synchronizes a specific TCC instance provided
	///     by the caller.
	static void wait_for_clock_sync();
	static void wait_for_tcc_sync(Tcc *regs);
	static int choose_prescaler(std::uint32_t period_ns, std::uint16_t *divisor,
		std::uint32_t *ctrla_bits, std::uint32_t *period_cycles);
	static int ns_to_cycles(std::uint32_t duration_ns, std::uint16_t prescaler_divisor,
		std::uint32_t *cycles);

	void configure_pin_mux() const;
	int program_waveform(std::uint32_t period_cycles, std::uint32_t pulse_cycles);

	const char *name_;
	Tcc *regs_;
	std::uint8_t channel_index_;
	std::uint8_t port_group_index_;
	std::uint8_t port_pin_index_;
	std::uint8_t pin_mux_value_;
	int backend_status_;
	bool configured_;
	bool enabled_;
	std::uint16_t prescaler_divisor_;
	std::uint32_t prescaler_bits_;
	std::uint32_t period_ns_;
	std::uint32_t period_cycles_;
	std::uint32_t pulse_cycles_;
};

} // namespace oshal::internal

#endif /* OSHAL_PWM_SAMD21_INTERNAL_HPP_ */