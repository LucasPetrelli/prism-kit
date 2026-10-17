#include "prism/set_single_color_hsv.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::SetSingleColorHsv::SetSingleColorHsv() {
  tag_ = prism::InstructionTag::kSetSingleColorHsv;
}

prism::SetSingleColorHsv::SetSingleColorHsv(
  const prism::SetSingleColorHsvPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      color{payload.h, payload.s, payload.v},
      index{payload.index} {
  tag_ = prism::InstructionTag::kSetSingleColorHsv;
}

std::uint32_t prism::SetSingleColorHsv::Execute(std::uint32_t) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  const color::RgbColor rgb = color::HsvToRgb(color);
  StripLed* led = strip->Led(index);
  if (led != nullptr) {
    led->SetColor(rgb);
  }
  controller->RequestShow();
  return 0U;
}

void prism::SetSingleColorHsv::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetSingleColorHsv(h=%u s=%u v=%u @%u)",
                static_cast<unsigned>(color.h), static_cast<unsigned>(color.s),
                static_cast<unsigned>(color.v), static_cast<unsigned>(index));
}
