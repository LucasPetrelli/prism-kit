#ifndef OSAL_TIME_H_
#define OSAL_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Sleep for the requested number of milliseconds.
/// @param duration_ms Duration to sleep, in milliseconds.
void osal_sleep_ms(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TIME_H_ */