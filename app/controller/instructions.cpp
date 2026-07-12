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
    case InstructionTag::kDelay:
      ::new (&delay) Delay(*static_cast<const Delay*>(instr));
      break;
  }
  tag = instr->Tag();
}

void prism::InstructionMemorySlot::SetStrip(Strip* s) {
  if (ControllerInstruction* instr = Active(); instr != nullptr) {
    instr->strip = s;
  }
}

void prism::InstructionMemorySlot::SetController(Controller* c) {
  if (ControllerInstruction* instr = Active(); instr != nullptr) {
    instr->controller = c;
  }
}

prism::ControllerInstruction* prism::InstructionMemorySlot::Active() {
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return &set_multiple_color;
    case InstructionTag::kSetSingleColor:
      return &set_single_color;
    case InstructionTag::kDelay:
      return &delay;
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
    case InstructionTag::kDelay:
      return &delay;
  }
  return nullptr;
}

std::uint32_t prism::InstructionMemorySlot::Execute() {
  return Active()->Execute();
}

void prism::InstructionMemorySlot::Destroy() {
  if (tag != InstructionTag{}) {
    Active()->~ControllerInstruction();
    tag = {};
  }
}

// ====================================================================
// SetMultipleColor::Execute
// ====================================================================

std::uint32_t prism::SetMultipleColor::Execute() {
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

// ====================================================================
// SetSingleColor::Execute
// ====================================================================

std::uint32_t prism::SetSingleColor::Execute() {
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

// ====================================================================
// Delay::Execute
// ====================================================================

std::uint32_t prism::Delay::Execute() {
  if (controller == nullptr) {
    return 0U;
  }

  if (start_time_ms_ == 0U) {
    // First call — capture start time, block, return full duration.
    start_time_ms_ = controller->GetTimestamp();
    controller->Block();
    return delay_ms_;
  }

  const std::uint32_t now = controller->GetTimestamp();
  const std::uint32_t elapsed = now - start_time_ms_;

  if (elapsed >= delay_ms_) {
    controller->Unblock();
    return 0U;
  }

  return delay_ms_ - elapsed;
}
