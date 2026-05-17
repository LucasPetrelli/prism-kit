#include <atomic>
#include <cstddef>
#include <cstdint>

#include "bal/led.hpp"
#include "bal/rgb_led.hpp"
#include "bal/ws2812_strip.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/status.h"
#include "oshal/time.hpp"
#include "prism_hw_backend_internal.hpp"

namespace app::internal {

namespace {

constexpr std::uint32_t kAppHwIdleSleepMs = 10U;
constexpr std::size_t kAppHwTaskStackSizeBytes = 1024U;
constexpr int kAppHwTaskPriority = 0;

struct PrismHwTaskState {
  bal::Ws2812Strip* backend_strip = nullptr;
  bal::Led* status_led = nullptr;
  oshal::DebugPort* debug_port = nullptr;
  std::uint32_t observed_generation = 0U;
};

PrismHwTaskState g_prism_hw_task_state = {};

/*
 * app_hw is the one APP-owned execution context allowed to mutate the BAL
 * strip. It polls the latest committed Prism Kit frame from the shared mailbox,
 * translates Prism colors into BAL colors, and then flushes BAL's staged frame
 * to the physical strip.
 */
int apply_frame(const SharedFrame& frame, bal::Ws2812Strip& backend_strip) {
  if (frame.led_count > backend_strip.led_count()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t index = 0; index < frame.led_count; ++index) {
    bal::Ws2812Led* const pixel = backend_strip.led(index);
    if ((pixel == nullptr) || !pixel->is_ready()) {
      return STATUS_ERR_DEVICE_UNAVAILABLE;
    }

    const prism::RgbColor& color = frame.colors[index];
    const int set_ret =
      pixel->set_color(bal::RgbColor{color.red, color.green, color.blue});
    if (set_ret < 0) {
      return set_ret;
    }
  }

  return backend_strip.show();
}

oshal::TaskConfig make_app_hw_task_config() {
  oshal::TaskConfig config;
  config.name = "app_hw";
  config.setup = prism_hw_task_setup;
  config.loop = prism_hw_task_loop;
  config.context = nullptr;
  config.stack_size_bytes = kAppHwTaskStackSizeBytes;
  config.priority = kAppHwTaskPriority;
  return config;
}

}  // namespace

SharedMailbox g_prism_hw_mailbox = {};
oshal::TaskHandle g_prism_hw_task;
RuntimeServices g_prism_runtime_services = {};

int ensure_prism_hw_started() {
  if (g_prism_hw_task.is_valid()) {
    return STATUS_OK;
  }

  return oshal::TaskHandle::create(g_prism_hw_task, make_app_hw_task_config());
}

int publish_prism_hw_frame(const SharedFrame& frame) {
  if (!g_prism_hw_task.is_valid()) {
    return STATUS_ERR_NOT_READY;
  }

  if (g_prism_hw_task.has_exited()) {
    int exit_code = STATUS_ERR_BACKEND;
    if (g_prism_hw_task.exit_code(&exit_code) < 0) {
      return STATUS_ERR_BACKEND;
    }

    return exit_code;
  }

  if (frame.led_count > kPrismHwMailboxFrameCapacity) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  const std::uint8_t next_frame_index = static_cast<std::uint8_t>(
    (g_prism_hw_mailbox.published_frame_index.load(std::memory_order_relaxed) ^
     1U) &
    0x1U);
  const std::uint32_t next_generation =
    g_prism_hw_mailbox.published_generation.load(std::memory_order_relaxed) +
    1U;
  g_prism_hw_mailbox.frames[next_frame_index] = frame;
  g_prism_hw_mailbox.published_frame_index.store(next_frame_index,
                                                 std::memory_order_release);
  g_prism_hw_mailbox.published_generation.store(next_generation,
                                                std::memory_order_release);
  return STATUS_OK;
}

bool prism_hw_task_setup(void* context) {
  static_cast<void>(context);

  g_prism_hw_task_state.backend_strip = &bal::ws2812_strip();
  g_prism_hw_task_state.status_led = g_prism_runtime_services.status_led;
  g_prism_hw_task_state.debug_port = g_prism_runtime_services.debug_port;

  if ((g_prism_hw_task_state.backend_strip == nullptr) ||
      !g_prism_hw_task_state.backend_strip->is_ready()) {
    return false;
  }

  if ((g_prism_hw_task_state.status_led == nullptr) ||
      !g_prism_hw_task_state.status_led->is_ready()) {
    return false;
  }

  if ((g_prism_hw_task_state.debug_port == nullptr) ||
      !g_prism_hw_task_state.debug_port->is_ready()) {
    return false;
  }

  if (g_prism_hw_task_state.debug_port->printf(
        "DebugPort online on %s, strip on %s\n",
        g_prism_hw_task_state.debug_port->name(),
        g_prism_hw_task_state.backend_strip->name()) < 0) {
    return false;
  }

  g_prism_hw_task_state.observed_generation =
    g_prism_hw_mailbox.published_generation.load(std::memory_order_acquire);
  return true;
}

bool prism_hw_task_loop(void* context) {
  static_cast<void>(context);

  if ((g_prism_hw_task_state.backend_strip == nullptr) ||
      (g_prism_hw_task_state.status_led == nullptr)) {
    return false;
  }

  const std::uint32_t published_generation =
    g_prism_hw_mailbox.published_generation.load(std::memory_order_acquire);
  if (published_generation != g_prism_hw_task_state.observed_generation) {
    const std::uint8_t published_frame_index =
      g_prism_hw_mailbox.published_frame_index.load(std::memory_order_acquire);
    const SharedFrame frame = g_prism_hw_mailbox.frames[published_frame_index];
    if (apply_frame(frame, *g_prism_hw_task_state.backend_strip) < 0) {
      return false;
    }

    if (g_prism_hw_task_state.status_led->toggle() < 0) {
      return false;
    }

    g_prism_hw_task_state.observed_generation = published_generation;
    return true;
  }

  oshal::sleep_ms(kAppHwIdleSleepMs);
  return true;
}

}  // namespace app::internal