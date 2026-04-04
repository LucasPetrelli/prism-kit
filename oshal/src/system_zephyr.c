#include <zephyr/init.h>

#include "oshal/gpio.h"
#include "oshal/system.h"
#include "oshal/status.h"

static int oshal_last_status = STATUS_ERR_NOT_READY;

static int oshal_system_init(void)
{
	/*
	 * Keep the SYS_INIT hook in C because it sits directly on Zephyr's startup
	 * boundary, but still publish the result through the OSHAL contract.
	 */
	if (!oshal_gpio_pin_is_ready(OSHAL_GPIO_PIN_PA17)) {
		oshal_last_status = STATUS_ERR_DEVICE_UNAVAILABLE;
		return oshal_last_status;
	}

	oshal_last_status = STATUS_OK;
	return STATUS_OK;
}

SYS_INIT(oshal_system_init, APPLICATION, 0);

bool oshal_system_ready(void)
{
	return oshal_last_status == STATUS_OK;
}

int oshal_system_status(void)
{
	return oshal_last_status;
}