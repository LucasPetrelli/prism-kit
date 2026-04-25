#include <array>
#include <cstdint>

#include "app/app.hpp"
#include "app/task_runtime_reporter.hpp"
#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/status.h"
#include "oshal/time.hpp"

namespace {

constexpr std::uint32_t kColorStepPeriodMs = 1000U;
constexpr std::uint32_t kRuntimeReportPeriodMs = 5000U;
constexpr std::uint32_t kRuntimeReportStepInterval =
  kRuntimeReportPeriodMs / kColorStepPeriodMs;
constexpr std::array<bal::RgbColor, 3U> kStripColors = {
  bal::RgbColor{255U, 0U, 0U},
  bal::RgbColor{0U, 0U, 255U},
  bal::RgbColor{0U, 255U, 0U},
};

static_assert(kRuntimeReportStepInterval > 0U,
              "Runtime report period must be at least one color step.");

}  // namespace

int app::run(void* context) {
  static_cast<void>(context);

  bal::Led& status_led = bal::status_led();
  bal::Ws2812Strip& demo_strip = bal::ws2812_strip();
  const oshal::TaskHandle app_task_handle = oshal::TaskHandle::current();
  app::TaskRuntimeReporter runtime_reporter(app_task_handle);
  bool reported_runtime_failure = false;
  std::uint32_t color_step_count = 0U;
  int ret;

  /*
   * APP depends on board-owned LED and strip objects instead of direct GPIO,
   * timer, or Zephyr driver details so the demo stays decoupled from board
   * wiring, wave-generation details, and the SAMD21 register map.
   */
  if (!status_led.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (!demo_strip.is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  ret = oshal::debug_port.printf("DebugPort online on %s, strip on %s\n",
                                 oshal::debug_port.name(), demo_strip.name());
  if (ret < 0) {
    return ret;
  }

  while (true) {
    const bal::RgbColor& next_color =
      kStripColors[color_step_count % kStripColors.size()];

    ret = demo_strip.fill(next_color);
    if (ret < 0) {
      return ret;
    }

    ret = demo_strip.show();
    if (ret < 0) {
      return ret;
    }

    ret = status_led.toggle();
    if (ret < 0) {
      return ret;
    }

    ++color_step_count;
    if ((color_step_count % kRuntimeReportStepInterval) == 0U) {
      const int diagnostics_ret = runtime_reporter.report();
      if ((diagnostics_ret < 0) && !reported_runtime_failure) {
        reported_runtime_failure = true;
        static_cast<void>(oshal::debug_port.printf(
          "Task runtime diagnostics unavailable (%d)\n", diagnostics_ret));
      }
    }

    oshal::sleep_ms(kColorStepPeriodMs);
  }
}