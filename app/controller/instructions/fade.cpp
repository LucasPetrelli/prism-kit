#include "prism/fade.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::Fade::Fade(const prism::FadePayload& payload)
    : prism::ControllerInstruction(payload.mark),
      percent{payload.percent},
      duration_ms{payload.duration_ms},
      index{payload.index} {
  tag_ = prism::InstructionTag::kFade;
  group_id = payload.group_id;
}

namespace {

constexpr std::uint32_t kFadeUpdateIntervalMs = 100U;

std::uint8_t InterpolateValue(std::uint8_t start, std::uint8_t target,
                              std::uint32_t elapsed, std::uint32_t duration) {
  const std::uint32_t difference =
    (target >= start) ? target - start : start - target;
  const std::uint32_t change =
    (difference * elapsed + duration / 2U) / duration;
  const std::uint32_t value =
    (target >= start) ? start + change : start - change;
  return static_cast<std::uint8_t>(value);
}

prism::color::RgbColor ScaleRgbColor(prism::color::RgbColor start,
                                     std::uint8_t value,
                                     std::uint8_t start_value) {
  if (start_value == 0U) {
    return prism::color::RgbColor{0U, 0U, 0U};
  }

  const auto scale_channel = [value, start_value](std::uint8_t channel) {
    const std::uint32_t scaled =
      static_cast<std::uint32_t>(channel) * value + start_value / 2U;
    return static_cast<std::uint8_t>(scaled / start_value);
  };
  return prism::color::RgbColor{scale_channel(start.red),
                                scale_channel(start.green),
                                scale_channel(start.blue)};
}

std::uint32_t DelayToFadeUpdate(std::uint32_t timestamp,
                                std::uint32_t remaining_ms) {
  const std::uint32_t phase = timestamp % kFadeUpdateIntervalMs;
  const std::uint32_t until_grid =
    (phase == 0U) ? kFadeUpdateIntervalMs : kFadeUpdateIntervalMs - phase;
  return (remaining_ms < until_grid) ? remaining_ms : until_grid;
}

}  // namespace

std::uint32_t prism::Fade::Execute(std::uint32_t timestamp) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  StripLed* led = strip->Led(index);
  if (led == nullptr) {
    return 0U;
  }

  if (!started_) {
    start_color_ = led->Color();
    start_value_ = start_color_.red;
    if (start_color_.green > start_value_) {
      start_value_ = start_color_.green;
    }
    if (start_color_.blue > start_value_) {
      start_value_ = start_color_.blue;
    }
    const std::uint32_t scaled_value =
      (static_cast<std::uint32_t>(start_value_) * percent + 50U) / 100U;
    target_value_ =
      static_cast<std::uint8_t>((scaled_value > 255U) ? 255U : scaled_value);
    start_time_ = timestamp;
    started_ = true;

    if (duration_ms == 0U) {
      led->SetColor(ScaleRgbColor(start_color_, target_value_, start_value_));
      controller->RequestShow();
      return 0U;
    }
    return DelayToFadeUpdate(timestamp, duration_ms);
  }

  const std::uint32_t elapsed = timestamp - start_time_;
  const bool complete = elapsed >= duration_ms;
  const std::uint8_t value =
    complete
      ? target_value_
      : InterpolateValue(start_value_, target_value_, elapsed, duration_ms);
  led->SetColor(ScaleRgbColor(start_color_, value, start_value_));
  controller->RequestShow();
  if (complete) {
    return 0U;
  }
  return DelayToFadeUpdate(timestamp, duration_ms - elapsed);
}

void prism::Fade::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "Fade(percent=%u duration=%u idx=%u)",
                static_cast<unsigned>(percent),
                static_cast<unsigned>(duration_ms),
                static_cast<unsigned>(index));
}

void prism::Fade::ResetForReplay() {
  started_ = false;
  start_time_ = 0U;
  start_color_ = color::RgbColor{0U, 0U, 0U};
  start_value_ = 0U;
  target_value_ = 0U;
}

bool prism::Fade::HasPositiveSchedulingBoundary() const {
  return duration_ms > 0U;
}
