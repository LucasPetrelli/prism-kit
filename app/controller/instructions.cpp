#include <new>

#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/strip.hpp"

// ====================================================================
// InstructionMemorySlot
// ====================================================================

prism::InstructionMemorySlot::~InstructionMemorySlot() { destroy(); }

void prism::InstructionMemorySlot::set(const ControllerInstruction* instr) {
  destroy();
  switch (instr->Tag()) {
    case InstructionTag::kSetMultipleColor:
      ::new (&setMultipleColor)
        SetMultipleColor(*static_cast<const SetMultipleColor*>(instr));
      break;
    case InstructionTag::kSetSingleColor:
      ::new (&setSingleColor)
        SetSingleColor(*static_cast<const SetSingleColor*>(instr));
      break;
  }
  tag_ = instr->Tag();
}

prism::ControllerInstruction* prism::InstructionMemorySlot::active() {
  switch (tag_) {
    case InstructionTag::kSetMultipleColor:
      return &setMultipleColor;
    case InstructionTag::kSetSingleColor:
      return &setSingleColor;
  }
  return nullptr;
}

const prism::ControllerInstruction* prism::InstructionMemorySlot::active()
  const {
  switch (tag_) {
    case InstructionTag::kSetMultipleColor:
      return &setMultipleColor;
    case InstructionTag::kSetSingleColor:
      return &setSingleColor;
  }
  return nullptr;
}

void prism::InstructionMemorySlot::execute() const { active()->Execute(); }

void prism::InstructionMemorySlot::destroy() {
  if (tag_ != InstructionTag{}) {
    active()->~ControllerInstruction();
    tag_ = {};
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
