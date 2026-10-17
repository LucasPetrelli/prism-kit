#include "prism/gradient.hpp"

#include <cstdint>
#include <cstdio>

#include "../oklab.hpp"
#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::Gradient::Gradient(const prism::GradientPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      target{payload.r, payload.g, payload.b},
      duration_ms{payload.duration_ms},
      index{payload.index} {
  tag_ = prism::InstructionTag::kGradient;
  group_id = payload.group_id;
}

namespace {

std::uint32_t DelayToGradientUpdate(std::uint32_t elapsed_ms,
                                    std::uint32_t remaining_ms,
                                    std::uint32_t refresh_period_ms) {
  const std::uint32_t phase = elapsed_ms % refresh_period_ms;
  const std::uint32_t until_grid =
    (phase == 0U) ? refresh_period_ms : refresh_period_ms - phase;
  return (remaining_ms < until_grid) ? remaining_ms : until_grid;
}

prism::controller_detail::OklabColor CachedOklab(std::int16_t l, std::int16_t a,
                                                 std::int16_t b) {
  return prism::controller_detail::OklabColor{l, a, b};
}

}  // namespace

std::uint32_t prism::Gradient::Execute(std::uint32_t timestamp) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  StripLed* led = strip->Led(index);
  if (led == nullptr) {
    return 0U;
  }

  if (!started_) {
    const color::RgbColor source = led->Color();
    start_time_ = timestamp;
    started_ = true;

    if (duration_ms == 0U) {
      led->SetColor(target);
      controller->RequestShow();
      return 0U;
    }

    const controller_detail::OklabColor source_lab =
      controller_detail::ToOklab(source);
    const controller_detail::OklabColor target_lab =
      controller_detail::ToOklab(target);
    source_l_ = source_lab.l;
    source_a_ = source_lab.a;
    source_b_ = source_lab.b;
    target_l_ = target_lab.l;
    target_a_ = target_lab.a;
    target_b_ = target_lab.b;
    return DelayToGradientUpdate(0U, duration_ms,
                                 controller->RefreshPeriodMs());
  }

  const std::uint32_t elapsed_ms = timestamp - start_time_;
  const bool complete = elapsed_ms >= duration_ms;
  color::RgbColor sample = target;
  if (!complete) {
    const auto lab = controller_detail::Interpolate(
      CachedOklab(source_l_, source_a_, source_b_),
      CachedOklab(target_l_, target_a_, target_b_),
      static_cast<std::uint16_t>(elapsed_ms), duration_ms);
    sample = controller_detail::FromOklab(lab);
  }

  led->SetColor(sample);
  controller->RequestShow();
  if (complete) {
    return 0U;
  }
  return DelayToGradientUpdate(elapsed_ms, duration_ms - elapsed_ms,
                               controller->RefreshPeriodMs());
}

void prism::Gradient::ToString(char* buf, std::size_t size) const {
  std::snprintf(
    buf, size, "Gradient(target=#%02X%02X%02X duration=%u idx=%u)",
    static_cast<unsigned>(target.red), static_cast<unsigned>(target.green),
    static_cast<unsigned>(target.blue), static_cast<unsigned>(duration_ms),
    static_cast<unsigned>(index));
}

void prism::Gradient::ResetForReplay() {
  started_ = false;
  start_time_ = 0U;
  source_l_ = 0;
  source_a_ = 0;
  source_b_ = 0;
  target_l_ = 0;
  target_a_ = 0;
  target_b_ = 0;
}

bool prism::Gradient::HasPositiveSchedulingBoundary() const {
  return duration_ms > 0U;
}
