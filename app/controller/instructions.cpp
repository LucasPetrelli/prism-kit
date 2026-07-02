#include <new>

#include "prism/controller.hpp"
#include "prism/strip.hpp"

// ====================================================================
// InstructionMemorySlot
// ====================================================================

prism::InstructionMemorySlot::~InstructionMemorySlot() { Destroy(); }

void prism::InstructionMemorySlot::Set(const ControllerInstruction* instr) {
  Destroy();
  switch (instr->Tag()) {
    case InstructionTag::kSetMultipleColor:
      ::new (&set_multiple_color)
        SetMultipleColor(*static_cast<const SetMultipleColor*>(instr));
      break;
    case InstructionTag::kSetSingleColor:
      ::new (&set_single_color)
        SetSingleColor(*static_cast<const SetSingleColor*>(instr));
      break;
  }
  tag = instr->Tag();
}

void prism::InstructionMemorySlot::SetStrip(Strip* s) {
  if (ControllerInstruction* instr = Active(); instr != nullptr) {
    instr->strip = s;
  }
}

prism::ControllerInstruction* prism::InstructionMemorySlot::Active() {
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return &set_multiple_color;
    case InstructionTag::kSetSingleColor:
      return &set_single_color;
  }
  return nullptr;
}

const prism::ControllerInstruction* prism::InstructionMemorySlot::Active()
  const {
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return &set_multiple_color;
    case InstructionTag::kSetSingleColor:
      return &set_single_color;
  }
  return nullptr;
}

void prism::InstructionMemorySlot::Execute() const { Active()->Execute(); }

void prism::InstructionMemorySlot::Destroy() {
  if (tag != InstructionTag{}) {
    Active()->~ControllerInstruction();
    tag = {};
  }
}

// ====================================================================
// SetMultipleColor::Execute
// ====================================================================

void prism::SetMultipleColor::Execute() const {
  if (strip == nullptr) {
    return;
  }
  for (std::uint32_t i = range.start; i < range.end; ++i) {
    StripLed* led = strip->Led(i);
    if (led != nullptr) {
      led->SetColor(color);
    }
  }
}

// ====================================================================
// SetSingleColor::Execute
// ====================================================================

void prism::SetSingleColor::Execute() const {
  if (strip == nullptr) {
    return;
  }
  StripLed* led = strip->Led(index);
  if (led != nullptr) {
    led->SetColor(color);
  }
}
