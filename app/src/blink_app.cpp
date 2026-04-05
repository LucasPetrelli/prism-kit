#include <cstdint>

#include "app/app.h"
#include "bal/led.hpp"
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

} // namespace

int app_run(void)
{
	bal::Led &status_led = bal::status_led();
	oshal::PwmOutput &demo_pwm = oshal::pa8_tcc0_wo0;
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

	while (true) {
		for (const std::uint8_t duty_cycle_percent : kDutyCyclePercents) {
			ret = status_led.toggle();
			if (ret < 0) {
				return ret;
			}

			ret = demo_pwm.set_pulse(pulse_width_for_percent(duty_cycle_percent));
			if (ret < 0) {
				return ret;
			}

			oshal::sleep_ms(kDemoStepPeriodMs);
		}
	}
}