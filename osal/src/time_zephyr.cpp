#include <cstdint>

#include <zephyr/kernel.h>

#include "osal/time.h"

void osal_sleep_ms(uint32_t duration_ms)
{
	/* Keep the RTOS primitive behind OSAL so APP never needs Zephyr headers. */
	k_msleep(static_cast<int32_t>(duration_ms));
}