#include <array>
#include <cstdint>

#include "app/app.hpp"
#include "prism/color.hpp"
#include "prism/controller.hpp"
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
  if (prism::Initialize() < 0) {
    return false;
  }

  prism::Strip& strip = prism::GetStrip();
  led_count_ = static_cast<std::uint8_t>(strip.LedCount());
  controller_.SetStrip(&strip);

  for (std::size_t i = 0U; i < instructions_.size(); ++i) {
    instructions_[i].color = kStripColors[i];
    instructions_[i].range.start = 0U;
    instructions_[i].range.end = led_count_;
  }
  return true;
}

bool AppTask::Loop() {
  controller_.ResetInstructions();
  controller_.AddInstruction(
    &instructions_[color_step_count_ % instructions_.size()]);
  controller_.Run();

  ++color_step_count_;
  prism::SleepMs(kColorStepPeriodMs);
  return true;
}

}  // namespace app