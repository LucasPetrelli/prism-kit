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
  switch (instr->tag()) {
    case InstructionTag::kSetMultipleColor:
      ::new (&setMultipleColor)
        SetMultipleColor(*static_cast<const SetMultipleColor*>(instr));
      break;
    case InstructionTag::kSetSingleColor:
      ::new (&setSingleColor)
        SetSingleColor(*static_cast<const SetSingleColor*>(instr));
      break;
  }
  tag_ = instr->tag();
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
  const RgbColor rgb = to_rgb(color);
  if (strip == nullptr) {
    return;
  }
  for (std::uint32_t i = range.start; i < range.end; ++i) {
    StripLed* led = strip->led(i);
    if (led != nullptr) {
      led->set_color(rgb);
    }
  }
}

// ====================================================================
// SetSingleColor::Execute
// ====================================================================

void prism::SetSingleColor::Execute() const {
  const RgbColor rgb = to_rgb(color);
  if (strip == nullptr) {
    return;
  }
  StripLed* led = strip->led(index);
  if (led != nullptr) {
    led->set_color(rgb);
  }
}
