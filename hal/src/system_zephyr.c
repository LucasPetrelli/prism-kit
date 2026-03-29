#include <zephyr/init.h>

#include "hal/gpio.h"
#include "hal/system.h"
#include "status/status.h"

static int hal_last_status = STATUS_ERR_NOT_READY;

static int hal_system_init(void)
{
	/*
	 * Keep the SYS_INIT hook in C because it sits directly on Zephyr's startup
	 * boundary, but still publish the result through the HAL contract.
	 */
	if (!hal_gpio_is_ready(HAL_GPIO_SIGNAL_STATUS_LED)) {
		hal_last_status = STATUS_ERR_DEVICE_UNAVAILABLE;
		return hal_last_status;
	}

	hal_last_status = STATUS_OK;
	return STATUS_OK;
}

SYS_INIT(hal_system_init, APPLICATION, 0);

bool hal_system_ready(void)
{
	return hal_last_status == STATUS_OK;
}

int hal_system_status(void)
{
	return hal_last_status;
}