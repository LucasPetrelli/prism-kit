#include <cstdint>
#include <iterator>

#include "app/app.hpp"
#include "bal/led.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/pwm.hpp"
#include "oshal/time.hpp"
#include "oshal/status.h"

namespace {

constexpr std::uint32_t kDemoStepPeriodMs = 1000U;
constexpr std::uint32_t kPwmPeriodNs = 1000000U;
constexpr std::uint8_t kDutyCyclePercents[] = {10U, 25U, 50U, 75U, 90U};

constexpr std::uint32_t pulse_width_for_percent(std::uint8_t duty_cycle_percent)
{
	return (kPwmPeriodNs * static_cast<std::uint32_t>(duty_cycle_percent)) / 100U;
}

constexpr std::uint32_t kPulseSequenceNs[] = {
	pulse_width_for_percent(kDutyCyclePercents[0]), pulse_width_for_percent(kDutyCyclePercents[1]),
	pulse_width_for_percent(kDutyCyclePercents[2]), pulse_width_for_percent(kDutyCyclePercents[3]),
	pulse_width_for_percent(kDutyCyclePercents[4]),
};

} // namespace

int app::run(void *context)
{
	static_cast<void>(context);

	bal::Led &status_led = bal::status_led();
	oshal::PwmOutput &demo_pwm = oshal::pa8_tcc0_wo0;
	oshal::PwmSequenceOutput &demo_pwm_sequence = oshal::pa8_tcc0_wo0_sequence;
	int ret;

	/*
	 * APP depends on board-owned LED and PWM objects instead of direct GPIO,
	 * timer, or Zephyr driver details so the demo stays decoupled from board
	 * wiring and the SAMD21 register map.
	 */
	if (!status_led.is_ready()) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	if (!demo_pwm.is_ready()) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	ret = demo_pwm.configure(kPwmPeriodNs, pulse_width_for_percent(kDutyCyclePercents[0]));
	if (ret < 0) {
		return ret;
	}

	ret = demo_pwm.enable();
	if (ret < 0) {
		return ret;
	}

	ret = demo_pwm_sequence.play_pulse_sequence(kPulseSequenceNs, std::size(kPulseSequenceNs), 0U);
	if (ret < 0) {
		return ret;
	}

	ret = oshal::debug_port.printf("DebugPort online on %s\n", oshal::debug_port.name());
	if (ret < 0) {
		return ret;
	}

	while (true) {
		ret = status_led.toggle();
		if (ret < 0) {
			return ret;
		}

		oshal::sleep_ms(kDemoStepPeriodMs);
	}
}