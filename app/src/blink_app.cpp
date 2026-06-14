#include <array>
#include <cstdint>

#include "app/app.hpp"
#include "prism/color.hpp"
#include "prism/strip.hpp"
#include "prism/time.hpp"

namespace {

constexpr std::uint32_t kColorStepPeriodMs = 1000U;
constexpr std::array<prism::RgbColor, 3U> kStripColors = {
  prism::RgbColor{255U, 0U, 0U},
  prism::RgbColor{0U, 0U, 255U},
  prism::RgbColor{0U, 255U, 0U},
};

struct BlinkAppState {
  std::uint32_t color_step_count = 0U;
};

BlinkAppState g_blink_app_state = {};

}  // namespace

bool app::setup(void* context) {
  static_cast<void>(context);

  g_blink_app_state.color_step_count = 0U;
  return prism::initialize() >= 0;
}

bool app::loop(void* context) {
  static_cast<void>(context);

  prism::Strip& demo_strip = prism::strip();
  const prism::RgbColor& next_color =
    kStripColors[g_blink_app_state.color_step_count % kStripColors.size()];

  if (demo_strip.fill(next_color) < 0) {
    return false;
  }

  if (demo_strip.show() < 0) {
    return false;
  }

  ++g_blink_app_state.color_step_count;
  prism::sleep_ms(kColorStepPeriodMs);
  return true;
}