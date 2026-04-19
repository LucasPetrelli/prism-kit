#include <zephyr/kernel.h>

#include <cstdint>

#include "oshal/time.h"

void oshal_sleep_ms(uint32_t duration_ms) {
  /* Keep the RTOS primitive behind OSHAL so APP never needs Zephyr headers. */
  k_msleep(static_cast<int32_t>(duration_ms));
}