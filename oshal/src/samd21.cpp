#include <zephyr/devicetree.h>

#include "oshal/gpio.hpp"
#include "oshal/pwm.hpp"
#include "gpio_zephyr_internal.hpp"
#include "pwm_samd21_internal.hpp"
#include "samd21_bridge.h"

#define GPIO_PA17_PORT_NODE DT_NODELABEL(porta)
#define GPIO_PA17_PIN 17U
#define PWM_PA8_TCC0_NODE DT_NODELABEL(tcc0)
#define PWM_PA8_PORT_GROUP_INDEX 0U
#define PWM_PA8_PIN 8U

#if !DT_NODE_HAS_STATUS_OKAY(GPIO_PA17_PORT_NODE)
#error "Unsupported board: GPIO controller for PA17 is not available"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(PWM_PA8_TCC0_NODE)
#error "Unsupported board: TCC0 is not available for PA8 PWM output"
#endif

namespace {

/* Keep board/controller-specific bindings here so the generic Zephyr backend stays reusable. */
oshal::internal::ZephyrGpio g_pa17{"PA17", {DEVICE_DT_GET(GPIO_PA17_PORT_NODE), GPIO_PA17_PIN, 0U}};
oshal::internal::Samd21PwmOutput g_pa8_tcc0_wo0{
	"PA8/TCC0_WO0", TCC0, 0U, PWM_PA8_PORT_GROUP_INDEX, PWM_PA8_PIN, MUX_PA08E_TCC0_WO0};

} // namespace

namespace oshal {

Gpio &pa17 = g_pa17;
PwmOutput &pa8_tcc0_wo0 = g_pa8_tcc0_wo0;

} // namespace oshal

bool oshal_gpio_pa17_is_ready(void)
{
	/* This bridge is intentionally narrow because Zephyr SYS_INIT still executes from C. */
	return oshal::pa17.is_ready();
}

int oshal_pwm_pa8_tcc0_wo0_init(void)
{
	/* This bridge keeps SYS_INIT in C while the PWM backend itself stays in C++. */
	return g_pa8_tcc0_wo0.initialize();
}

bool oshal_pwm_pa8_tcc0_wo0_is_ready(void)
{
	return oshal::pa8_tcc0_wo0.is_ready();
}