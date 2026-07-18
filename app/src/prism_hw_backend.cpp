#include <cstddef>

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "hw/command_manager.hpp"
#include "hw/hw_constants.hpp"
#include "hw/hw_coordinator.hpp"
#include "hw/hw_task.hpp"
#include "hw/shared_frame.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/serial_port.hpp"
#include "oshal/status.h"
#include "oshal/task.hpp"
#include "oshal/time.hpp"
#include "prism/strip.hpp"
#include "prism/time.hpp"

namespace prism {

int Initialize() {
  static bool initialized = false;
  if (initialized) {
    return STATUS_OK;
  }

  if (!oshal::TaskHandle::Current().IsValid()) {
    return STATUS_ERR_NOT_READY;
  }

  bal::Ws2812Strip& backend_strip = bal::GetWs2812Strip();
  if (!backend_strip.IsReady()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  bal::Led& status_led = bal::StatusLed();
  if (!status_led.IsReady()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (!oshal::debug_port.IsReady()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if ((oshal::command_port != nullptr) && !oshal::command_port->IsReady()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  const std::size_t led_count = backend_strip.LedCount();
  if (led_count > app::hw::kSharedFrameCapacity) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  /* HwTask::Setup() configures the HW managers when the executor task
   * starts.  Start the HW executor to kick that off. */
  const int start_ret = app::hw::StartHwExecutor();
  if (start_ret < 0) {
    return start_ret;
  }

  initialized = true;
  return STATUS_OK;
}

Strip& GetStrip() { return app::hw::HwTask::Instance().GetStrip(); }

void SleepMs(std::uint32_t duration_ms) { oshal::SleepMs(duration_ms); }

std::uint32_t UptimeMs() { return oshal::UptimeMs(); }

}  // namespace prism