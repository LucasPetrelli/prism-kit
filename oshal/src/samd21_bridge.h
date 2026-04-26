#ifndef OSHAL_SAMD21_BRIDGE_H_
#define OSHAL_SAMD21_BRIDGE_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool oshal_status_gpio_is_ready(void);
int oshal_strip_pwm_output_init(void);
bool oshal_strip_pwm_output_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_SAMD21_BRIDGE_H_ */