#include <zephyr/devicetree.h>

#include "oshal/debug_port.hpp"
#include "oshal/samd21_resources.hpp"
#include "samd21_bridge.h"
#include "samd21_pwm_internal.hpp"
#include "samd21_ws2812_internal.hpp"
#include "zephyr_debug_port_cdc_acm_internal.hpp"
#include "zephyr_gpio_internal.hpp"

#define DEBUG_PORT_NODE DT_CHOSEN(zephyr_console)

#if !DT_NODE_HAS_COMPAT(DEBUG_PORT_NODE, zephyr_cdc_acm_uart)
#include "zephyr_debug_port_internal.hpp"
#endif

#define GPIO_PA17_PORT_NODE DT_NODELABEL(porta)
#define GPIO_PA17_PIN 17U
#define PWM_PA8_TCC0_NODE DT_NODELABEL(tcc0)
#define PWM_PA8_PORT_GROUP_INDEX 0U
#define PWM_PA8_PIN 8U

#if !DT_HAS_CHOSEN(zephyr_console)
#error "Unsupported board: zephyr,console is not defined"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(DEBUG_PORT_NODE)
#error "Unsupported board: zephyr,console is not available"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(GPIO_PA17_PORT_NODE)
#error "Unsupported board: GPIO controller for PA17 is not available"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(PWM_PA8_TCC0_NODE)
#error "Unsupported board: TCC0 is not available for PA8 PWM output"
#endif

namespace {

/* Keep board/controller-specific bindings here so the generic Zephyr backend
 * stays reusable. */
#if DT_NODE_HAS_COMPAT(DEBUG_PORT_NODE, zephyr_cdc_acm_uart)
oshal::internal::ZephyrCdcAcmDebugPort g_debug_port{
  "zephyr.console.cdc_acm", DEVICE_DT_GET(DEBUG_PORT_NODE)};
#else
oshal::internal::ZephyrDebugPort g_debug_port{"zephyr.console",
                                              DEVICE_DT_GET(DEBUG_PORT_NODE)};
#endif
oshal::internal::ZephyrGpio g_status_gpio{
  "PA17", {DEVICE_DT_GET(GPIO_PA17_PORT_NODE), GPIO_PA17_PIN, 0U}};
oshal::internal::Samd21DmaPwmOutput g_strip_pwm_output{
  "PA8/TCC0_WO0",     TCC0, 0U, PWM_PA8_PORT_GROUP_INDEX, PWM_PA8_PIN,
  MUX_PA08E_TCC0_WO0, 0U};
oshal::internal::Samd21PwmWs2812Transport g_strip_ws2812_transport{
  "PA8/TCC0_WO0.ws2812", g_strip_pwm_output, g_strip_pwm_output};

}  // namespace

namespace oshal {

DebugPort& debug_port = g_debug_port;
Gpio& samd21_gpio_pa17 = g_status_gpio;
PwmOutput& samd21_pwm_pa8_tcc0_wo0 = g_strip_pwm_output;
PwmSequenceOutput& samd21_pwm_pa8_tcc0_wo0_sequence_output = g_strip_pwm_output;
Ws2812Transport& samd21_ws2812_pa8_tcc0_wo0_transport =
  g_strip_ws2812_transport;

}  // namespace oshal

bool oshal_samd21_gpio_pa17_is_ready(void) {
  /* This bridge is intentionally narrow because Zephyr SYS_INIT still executes
   * from C. */
  return oshal::samd21_gpio_pa17.is_ready();
}

int oshal_samd21_pwm_pa8_tcc0_wo0_init(void) {
  /* This bridge keeps SYS_INIT in C while the PWM backend itself stays in C++.
   */
  return g_strip_pwm_output.initialize();
}

bool oshal_samd21_pwm_pa8_tcc0_wo0_is_ready(void) {
  return oshal::samd21_pwm_pa8_tcc0_wo0.is_ready();
}