#include <cstdint>
#include <cstdio>

#include "prism/color.hpp"
#include "prism/controller.hpp"
#include "prism/instruction.hpp"
#include "prism/strip.hpp"

// ====================================================================
// InstructionMemorySlot
// ====================================================================

prism::InstructionMemorySlot::~InstructionMemorySlot() { Destroy(); }

bool prism::InstructionMemorySlot::Set(const ControllerInstruction* instr) {
  Destroy();
  if (instr == nullptr) {
    return false;
  }

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
    case InstructionTag::kRunGroup:
      ::new (&run_group)
        RunGroupInstruction(*static_cast<const RunGroupInstruction*>(instr));
      break;
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      ::new (&test_timed) test::TimedInstruction(
        *static_cast<const test::TimedInstruction*>(instr));
      break;
#endif
    default:
      return false;
  }
  tag = instr->Tag();
  active_ = true;
  return true;
}

void prism::InstructionMemorySlot::Clear() { Destroy(); }

prism::Mark prism::InstructionMemorySlot::GetMark() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->mark : Mark{0U};
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

prism::GroupId prism::InstructionMemorySlot::GetGroupId() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->group_id : GroupId{0U};
}

bool prism::InstructionMemorySlot::IsRunGroup() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr && instr->IsRunGroup();
}

prism::GroupId prism::InstructionMemorySlot::GetTargetGroupId() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->TargetGroupId() : GroupId{0U};
}

prism::LoopCount prism::InstructionMemorySlot::GetAdditionalRepeats() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->AdditionalRepeats() : LoopCount{0U};
}

bool prism::InstructionMemorySlot::HasPositiveSchedulingBoundary() const {
  const ControllerInstruction* instr = Active();
  return instr != nullptr && instr->HasPositiveSchedulingBoundary();
}

void prism::InstructionMemorySlot::ResetForReplay() {
  if (ControllerInstruction* instr = Active(); instr != nullptr) {
    instr->ResetForReplay();
  }
}

prism::ControllerInstruction* prism::InstructionMemorySlot::Active() {
  if (!active_) {
    return nullptr;
  }
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return &set_multiple_color;
    case InstructionTag::kSetSingleColor:
      return &set_single_color;
    case InstructionTag::kSetMultipleColorHsv:
      return &set_multiple_color_hsv;
    case InstructionTag::kSetSingleColorHsv:
      return &set_single_color_hsv;
    case InstructionTag::kRunGroup:
      return &run_group;
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      return &test_timed;
#endif
  }
  return nullptr;
}

const prism::ControllerInstruction* prism::InstructionMemorySlot::Active()
  const {
  if (!active_) {
    return nullptr;
  }
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      return &set_multiple_color;
    case InstructionTag::kSetSingleColor:
      return &set_single_color;
    case InstructionTag::kSetMultipleColorHsv:
      return &set_multiple_color_hsv;
    case InstructionTag::kSetSingleColorHsv:
      return &set_single_color_hsv;
    case InstructionTag::kRunGroup:
      return &run_group;
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      return &test_timed;
#endif
  }
  return nullptr;
}

std::uint32_t prism::InstructionMemorySlot::Execute() {
  ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->Execute() : 0U;
}

void prism::InstructionMemorySlot::ToString(char* buf, std::size_t size) const {
  const ControllerInstruction* instr = Active();
  if (instr != nullptr) {
    instr->ToString(buf, size);
    return;
  }
  std::snprintf(buf, size, "Empty");
}

void prism::InstructionMemorySlot::Destroy() {
  if (!active_) {
    return;
  }
  switch (tag) {
    case InstructionTag::kSetMultipleColor:
      set_multiple_color.~SetMultipleColor();
      break;
    case InstructionTag::kSetSingleColor:
      set_single_color.~SetSingleColor();
      break;
    case InstructionTag::kSetMultipleColorHsv:
      set_multiple_color_hsv.~SetMultipleColorHsv();
      break;
    case InstructionTag::kSetSingleColorHsv:
      set_single_color_hsv.~SetSingleColorHsv();
      break;
    case InstructionTag::kRunGroup:
      run_group.~RunGroupInstruction();
      break;
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      test_timed.~TimedInstruction();
      break;
#endif
    default:
      break;
  }
  active_ = false;
  tag = {};
}

// ====================================================================
// PendingInstructionQueueView
// ====================================================================

void prism::PendingInstructionQueueView::Insert(std::uint32_t slot_idx) {
  // Insertion-sort by ascending mark.  Linear scan is fine because
  // sorted_count_ is capped at Controller::kMaxInstruction.
  std::uint32_t ins = 0U;
  while (ins < sorted_count_ && instructions_[sorted_order_[ins]].GetMark() <=
                                  instructions_[slot_idx].GetMark()) {
    ++ins;
  }
  for (std::uint32_t i = sorted_count_; i > ins; --i) {
    sorted_order_[i] = sorted_order_[i - 1U];
  }
  sorted_order_[ins] = slot_idx;
  ++sorted_count_;
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
    case InstructionTag::kRunGroup:
      return "RunGroup";
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      return "TimedInstruction";
#endif
  }
  return "Unknown";
}

// ====================================================================
// ControllerInstruction lifecycle and metadata
// ====================================================================

void prism::ControllerInstruction::ResetForReplay() {}

bool prism::ControllerInstruction::HasPositiveSchedulingBoundary() const {
  return false;
}

bool prism::ControllerInstruction::IsRunGroup() const { return false; }

prism::GroupId prism::ControllerInstruction::TargetGroupId() const {
  return GroupId{0U};
}

prism::LoopCount prism::ControllerInstruction::AdditionalRepeats() const {
  return LoopCount{0U};
}

// ====================================================================
// GroupRuntime
// ====================================================================

void prism::GroupRuntime::Reset() {
  cursor = 0U;
  start_time = 0U;
  repeats_remaining = 0U;
  active_count = 0U;
  child_count = 0U;
  positive_boundary_observed = false;
  parent_group_index = prism::kMaxGroups;
  depth = 0U;
  active = false;
}

// ====================================================================
// RunGroupInstruction
// ====================================================================

std::uint32_t prism::RunGroupInstruction::Execute() { return 0U; }

void prism::RunGroupInstruction::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "RunGroup(target=%u repeats=%u)",
                static_cast<unsigned>(target_id),
                static_cast<unsigned>(additional_repeats));
}

bool prism::RunGroupInstruction::IsRunGroup() const { return true; }

prism::GroupId prism::RunGroupInstruction::TargetGroupId() const {
  return target_id;
}

prism::LoopCount prism::RunGroupInstruction::AdditionalRepeats() const {
  return additional_repeats;
}

#if defined(PRISM_CONTROLLER_TESTING)
prism::test::TimedInstruction::TimedInstruction(std::uint32_t delay_ms,
                                                std::uint32_t* execution_count,
                                                bool yield_on_first_execution)
    : delay_ms_{delay_ms},
      execution_count_{execution_count},
      yield_on_first_execution_{yield_on_first_execution} {
  tag_ = InstructionTag::kTestTimed;
}

std::uint32_t prism::test::TimedInstruction::Execute() {
  if (execution_count_ != nullptr) {
    ++*execution_count_;
  }
  if (!yield_on_first_execution_ || yielded_) {
    return 0U;
  }
  yielded_ = true;
  return delay_ms_;
}

void prism::test::TimedInstruction::ToString(char* buf,
                                             std::size_t size) const {
  std::snprintf(buf, size, "TimedInstruction(delay=%u)",
                static_cast<unsigned>(delay_ms_));
}

void prism::test::TimedInstruction::ResetForReplay() { yielded_ = false; }

bool prism::test::TimedInstruction::HasPositiveSchedulingBoundary() const {
  return delay_ms_ > 0U;
}
#endif

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
