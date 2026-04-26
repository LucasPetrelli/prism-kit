#ifndef OSHAL_SAMD21_BRIDGE_H_
#define OSHAL_SAMD21_BRIDGE_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool oshal_samd21_gpio_pa17_is_ready(void);
int oshal_samd21_pwm_pa8_tcc0_wo0_init(void);
bool oshal_samd21_pwm_pa8_tcc0_wo0_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_SAMD21_BRIDGE_H_ */