#include <cstddef>

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "hw/command_manager.hpp"
#include "hw/hw_constants.hpp"
#include "hw/hw_coordinator.hpp"
#include "hw/shared_frame.hpp"
#include "hw/status_led.hpp"
#include "hw/strip_manager.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/serial_port.hpp"
#include "oshal/status.h"
#include "oshal/task.hpp"
#include "oshal/time.hpp"
#include "prism/color.hpp"
#include "prism/strip.hpp"
#include "prism/time.hpp"

namespace prism {

int initialize() {
  static bool initialized = false;
  if (initialized) {
    return STATUS_OK;
  }

  if (!oshal::TaskHandle::current().is_valid()) {
    return STATUS_ERR_NOT_READY;
  }

  bal::Ws2812Strip& backend_strip = bal::ws2812_strip();
  if (!backend_strip.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  bal::Led& status_led = bal::status_led();
  if (!status_led.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (!oshal::debug_port.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if ((oshal::command_port != nullptr) && !oshal::command_port->is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  const std::size_t led_count = backend_strip.led_count();
  if (led_count > app::hw::kSharedFrameCapacity) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  /* Configure the HW managers before starting the executor task.
   * StripManager implements prism::Strip — APP code calls fill()/show()
   * on it directly, and show() posts the committed frame to the internal
   * mailbox for the app_hw task to drain. */
  app::hw::StripManager::Instance().Configure(&backend_strip, led_count,
                                              backend_strip.name());
  app::hw::CommandManager::Instance().Configure(
    oshal::command_port, &oshal::debug_port,
    &app::hw::StripManager::Instance().event_group());
  app::hw::StatusLed::Instance().Configure(&status_led,
                                           app::hw::kTaskIdleSleepMs);

  /* Start the HW executor before publishing any committed strip frame. */
  const int start_ret = app::hw::StartHwExecutor();
  if (start_ret < 0) {
    return start_ret;
  }

  initialized = true;
  return STATUS_OK;
}

Strip& strip() { return app::hw::StripManager::Instance(); }

void sleep_ms(std::uint32_t duration_ms) { oshal::sleep_ms(duration_ms); }

}  // namespace prism