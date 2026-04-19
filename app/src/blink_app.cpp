#include <cstdint>
#include <iterator>

#include "app/app.hpp"
#include "app/task_runtime_reporter.hpp"
#include "bal/led.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/pwm.hpp"
#include "oshal/status.h"
#include "oshal/time.hpp"

namespace {

constexpr std::uint32_t kDemoStepPeriodMs = 1000U;
constexpr std::uint32_t kRuntimeReportPeriodMs = 5000U;
constexpr std::uint32_t kRuntimeReportStepInterval = kRuntimeReportPeriodMs / kDemoStepPeriodMs;
constexpr std::uint32_t kPwmPeriodNs = 1000000U;
constexpr std::uint8_t kDutyCyclePercents[] = {10U, 25U, 50U, 75U, 90U};

static_assert(kRuntimeReportStepInterval > 0U,
	"Runtime report period must be at least one demo step.");

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
	const oshal::TaskHandle app_task_handle = oshal::TaskHandle::current();
	app::TaskRuntimeReporter runtime_reporter(app_task_handle);
	bool reported_runtime_failure = false;
	std::uint32_t demo_step_count = 0U;
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

		++demo_step_count;
		if ((demo_step_count % kRuntimeReportStepInterval) == 0U) {
			const int diagnostics_ret = runtime_reporter.report();
			if ((diagnostics_ret < 0) && !reported_runtime_failure) {
				reported_runtime_failure = true;
				static_cast<void>(oshal::debug_port.printf(
					"Task runtime diagnostics unavailable (%d)\n", diagnostics_ret));
			}
		}

		oshal::sleep_ms(kDemoStepPeriodMs);
	}
}