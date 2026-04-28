#include <array>
#include <cstdint>

#include "app/app.hpp"
#include "prism/strip.hpp"
#include "prism/time.hpp"

namespace {

constexpr std::uint32_t kColorStepPeriodMs = 1000U;
constexpr std::array<prism::RgbColor, 3U> kStripColors = {
  prism::RgbColor{255U, 0U, 0U},
  prism::RgbColor{0U, 0U, 255U},
  prism::RgbColor{0U, 255U, 0U},
};

}  // namespace

int app::run(void* context) {
  static_cast<void>(context);

  std::uint32_t color_step_count = 0U;
  int ret = 0;

  ret = prism::initialize();
  if (ret < 0) {
    return ret;
  }

  prism::Strip& demo_strip = prism::strip();

  while (true) {
    const prism::RgbColor& next_color =
      kStripColors[color_step_count % kStripColors.size()];

    ret = demo_strip.fill(next_color);
    if (ret < 0) {
      return ret;
    }

    ret = demo_strip.show();
    if (ret < 0) {
      return ret;
    }

    ++color_step_count;
    prism::sleep_ms(kColorStepPeriodMs);
  }
}