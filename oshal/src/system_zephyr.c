#include <zephyr/init.h>

#include "oshal/pwm.h"
#include "oshal/system.h"
#include "oshal/status.h"
#include "pwm_backend.h"

/* Keep the GPIO readiness bridge local because C callers outside startup no longer exist. */
extern bool oshal_gpio_pa17_is_ready(void);

static int oshal_last_status = STATUS_ERR_NOT_READY;

static int oshal_system_init(void)
{
	int ret;

	/*
	 * Keep the SYS_INIT hook in C because it sits directly on Zephyr's startup
	 * boundary, but still publish the result through the OSHAL contract.
	 */
	ret = oshal_pwm_backend_init();
	if (ret < 0) {
		oshal_last_status = ret;
		return oshal_last_status;
	}

	/* Treat missing PA8 PWM ownership as a startup fault, not a deferred runtime surprise. */
	if (!oshal_pwm_output_is_ready(OSHAL_PWM_OUTPUT_PA8_TCC0_WO0)) {
		oshal_last_status = STATUS_ERR_DEVICE_UNAVAILABLE;
		return oshal_last_status;
	}

	/* Keep the phase-1 GPIO path in the same startup gate so regressions stay visible. */
	if (!oshal_gpio_pa17_is_ready()) {
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