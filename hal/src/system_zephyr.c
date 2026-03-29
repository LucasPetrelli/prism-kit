#include <errno.h>

#include <zephyr/init.h>

#include "hal/gpio.h"
#include "hal/system.h"

static int hal_last_status = -EAGAIN;

static int hal_system_init(void)
{
	/*
	 * Keep the SYS_INIT hook in C because it sits directly on Zephyr's startup
	 * boundary, but still publish the result through the HAL contract.
	 */
	if (!hal_gpio_is_ready(HAL_GPIO_SIGNAL_STATUS_LED)) {
		hal_last_status = -ENODEV;
		return hal_last_status;
	}

	hal_last_status = 0;
	return 0;
}

SYS_INIT(hal_system_init, APPLICATION, 0);

bool hal_system_ready(void)
{
	return hal_last_status == 0;
}

int hal_system_status(void)
{
	return hal_last_status;
}