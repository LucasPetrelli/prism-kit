#include <zephyr/devicetree.h>

#include "oshal/gpio.hpp"
#include "gpio_zephyr_internal.hpp"

#define GPIO_PA17_PORT_NODE DT_NODELABEL(porta)
#define GPIO_PA17_PIN 17U

#if !DT_NODE_HAS_STATUS_OKAY(GPIO_PA17_PORT_NODE)
#error "Unsupported board: GPIO controller for PA17 is not available"
#endif

namespace {

/* Keep board/controller-specific bindings here so the generic Zephyr backend stays reusable. */
oshal::internal::ZephyrGpio g_pa17{"PA17", {DEVICE_DT_GET(GPIO_PA17_PORT_NODE), GPIO_PA17_PIN, 0U}};

} // namespace

namespace oshal {

Gpio &pa17 = g_pa17;

} // namespace oshal

extern "C" bool oshal_gpio_pa17_is_ready(void)
{
	/* This bridge is intentionally narrow because Zephyr SYS_INIT still executes from C. */
	return oshal::pa17.is_ready();
}