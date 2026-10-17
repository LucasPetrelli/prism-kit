#include "prism/set_multiple_color.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/controller.hpp"
#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

prism::SetMultipleColor::SetMultipleColor() {
  tag_ = prism::InstructionTag::kSetMultipleColor;
}

prism::SetMultipleColor::SetMultipleColor(
  const prism::SetMultipleColorPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      color{payload.r, payload.g, payload.b},
      range{payload.range} {
  tag_ = prism::InstructionTag::kSetMultipleColor;
}

std::uint32_t prism::SetMultipleColor::Execute(std::uint32_t) {
  if (controller == nullptr || strip == nullptr) {
    return 0U;
  }
  for (std::uint32_t i = range.start; i < range.end; ++i) {
    StripLed* led = strip->Led(i);
    if (led != nullptr) {
      led->SetColor(color);
    }
  }
  controller->RequestShow();
  return 0U;
}

void prism::SetMultipleColor::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetMultipleColor(r=%u g=%u b=%u [%u,%u))",
                color.red, color.green, color.blue, range.start, range.end);
}
