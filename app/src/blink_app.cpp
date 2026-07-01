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

}  // namespace

namespace app {

AppTask& AppTask::Instance() {
  static AppTask instance;
  return instance;
}

bool AppTask::SetupTrampoline(void* context) {
  static_cast<void>(context);
  return Instance().Setup();
}

bool AppTask::LoopTrampoline(void* context) {
  static_cast<void>(context);
  return Instance().Loop();
}

bool AppTask::Setup() {
  color_step_count_ = 0U;
  return prism::Initialize() >= 0;
}

bool AppTask::Loop() {
  prism::Strip& demo_strip = prism::GetStrip();
  const prism::RgbColor& next_color =
    kStripColors[color_step_count_ % kStripColors.size()];

  if (demo_strip.Fill(next_color) < 0) {
    return false;
  }

  if (demo_strip.Show() < 0) {
    return false;
  }

  ++color_step_count_;
  prism::SleepMs(kColorStepPeriodMs);
  return true;
}

}  // namespace app