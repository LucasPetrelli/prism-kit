#include "prism/set_single_color.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::SetSingleColor::SetSingleColor() {
  tag_ = prism::InstructionTag::kSetSingleColor;
}

prism::SetSingleColor::SetSingleColor(
  const prism::SetSingleColorPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      color{payload.r, payload.g, payload.b},
      index{payload.index} {
  tag_ = prism::InstructionTag::kSetSingleColor;
}

std::uint32_t prism::SetSingleColor::Execute(std::uint32_t) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  StripLed* led = strip->Led(index);
  if (led != nullptr) {
    led->SetColor(color);
  }
  controller->RequestShow();
  return 0U;
}

void prism::SetSingleColor::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetSingleColor(r=%u g=%u b=%u idx=%u)", color.red,
                color.green, color.blue, index);
}
