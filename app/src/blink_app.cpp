#include <cerrno>

#include "app/app.h"
#include "bal/led.hpp"
#include "osal/time.hpp"

namespace {

constexpr auto kBlinkPeriodMs = 250U;

} // namespace

int app_run(void)
{
	const bal_led_t *status_led = nullptr;

	/*
	 * APP depends on a board-owned LED handle instead of a raw GPIO or Zephyr
	 * alias so the product logic stays decoupled from board wiring and RTOS
	 * details.
	 */
	status_led = bal::status_led();
	if (status_led == nullptr) {
		return -ENODEV;
	}

	while (true) {
		const int ret = bal::toggle_led(*status_led);
		if (ret < 0) {
			return ret;
		}

		osal::sleep_ms(kBlinkPeriodMs);
	}
}