#include <cstdint>
#include <cstdio>

#include "prism/color.hpp"
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
    case InstructionTag::kSetMultipleColorHsv:
      ::new (&set_multiple_color_hsv)
        SetMultipleColorHsv(*static_cast<const SetMultipleColorHsv*>(instr));
      break;
    case InstructionTag::kSetSingleColorHsv:
      ::new (&set_single_color_hsv)
        SetSingleColorHsv(*static_cast<const SetSingleColorHsv*>(instr));
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
    case InstructionTag::kSetMultipleColorHsv:
      return &set_multiple_color_hsv;
    case InstructionTag::kSetSingleColorHsv:
      return &set_single_color_hsv;
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
    case InstructionTag::kSetMultipleColorHsv:
      return &set_multiple_color_hsv;
    case InstructionTag::kSetSingleColorHsv:
      return &set_single_color_hsv;
    case InstructionTag::kDelay:
      return &delay;
  }
  return nullptr;
}

std::uint32_t prism::InstructionMemorySlot::Execute() {
  return Active()->Execute();
}

void prism::InstructionMemorySlot::ToString(char* buf, std::size_t size) const {
  Active()->ToString(buf, size);
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
// SetMultipleColorHsv::Execute
// ====================================================================

std::uint32_t prism::SetMultipleColorHsv::Execute() {
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

// ====================================================================
// SetSingleColorHsv::Execute
// ====================================================================

std::uint32_t prism::SetSingleColorHsv::Execute() {
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

// ====================================================================
// ToString
// ====================================================================

const char* prism::InstructionToString(InstructionTag tag) {
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return "SetMultipleColor";
    case InstructionTag::kSetSingleColor:
      return "SetSingleColor";
    case InstructionTag::kSetMultipleColorHsv:
      return "SetMultipleColorHsv";
    case InstructionTag::kSetSingleColorHsv:
      return "SetSingleColorHsv";
    case InstructionTag::kDelay:
      return "Delay";
  }
  return "Unknown";
}

// ====================================================================
// ControllerInstruction::ToString  (default — tag name only)
// ====================================================================

void prism::ControllerInstruction::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "%s", prism::InstructionToString(tag_));
}

// ====================================================================
// SetMultipleColorHsv::ToString
// ====================================================================

void prism::SetMultipleColorHsv::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetMultipleColorHsv(h=%u s=%u v=%u [%u..%u))",
                static_cast<unsigned>(color.h), static_cast<unsigned>(color.s),
                static_cast<unsigned>(color.v),
                static_cast<unsigned>(range.start),
                static_cast<unsigned>(range.end));
}

// ====================================================================
// SetSingleColorHsv::ToString
// ====================================================================

void prism::SetSingleColorHsv::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetSingleColorHsv(h=%u s=%u v=%u @%u)",
                static_cast<unsigned>(color.h), static_cast<unsigned>(color.s),
                static_cast<unsigned>(color.v), static_cast<unsigned>(index));
}

// ====================================================================
// SetMultipleColor::ToString
// ====================================================================

void prism::SetMultipleColor::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetMultipleColor(r=%u g=%u b=%u [%u,%u))",
                color.red, color.green, color.blue, range.start, range.end);
}

// ====================================================================
// SetSingleColor::ToString
// ====================================================================

void prism::SetSingleColor::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "SetSingleColor(r=%u g=%u b=%u idx=%u)", color.red,
                color.green, color.blue, index);
}

// ====================================================================
// Delay::ToString
// ====================================================================

void prism::Delay::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "Delay(%u ms)", delay_ms_);
}
