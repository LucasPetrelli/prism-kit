#include "prism/set_multiple_color_hsv.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::SetMultipleColorHsv::SetMultipleColorHsv() {
  tag_ = prism::InstructionTag::kSetMultipleColorHsv;
}

prism::SetMultipleColorHsv::SetMultipleColorHsv(
  const prism::SetMultipleColorHsvPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      color{payload.h, payload.s, payload.v},
      range{payload.range} {
  tag_ = prism::InstructionTag::kSetMultipleColorHsv;
}

std::uint32_t prism::SetMultipleColorHsv::Execute(std::uint32_t) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  const color::RgbColor rgb = color::HsvToRgb(color);
  for (std::uint32_t i = range.start; i < range.end; ++i) {
    StripLed* led = strip->Led(i);
    if (led != nullptr) {
      led->SetColor(rgb);
    }
  }
  controller->RequestShow();
  return 0U;
}

void prism::SetMultipleColorHsv::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetMultipleColorHsv(h=%u s=%u v=%u [%u..%u))",
                static_cast<unsigned>(color.h), static_cast<unsigned>(color.s),
                static_cast<unsigned>(color.v),
                static_cast<unsigned>(range.start),
                static_cast<unsigned>(range.end));
}
