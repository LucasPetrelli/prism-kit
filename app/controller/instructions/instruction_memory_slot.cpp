#include "prism/instruction_memory_slot.hpp"

#include <cstdint>
#include <cstdio>

#include "prism/controller_instruction.hpp"
#include "prism/fade.hpp"
#include "prism/instruction_types.hpp"
#include "prism/run_group_instruction.hpp"
#include "prism/set_multiple_color.hpp"
#include "prism/set_multiple_color_hsv.hpp"
#include "prism/set_single_color.hpp"
#include "prism/set_single_color_hsv.hpp"
#include "prism/strip.hpp"

prism::InstructionMemorySlot::InstructionMemorySlot() : tag{}, active_{false} {}

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
    case InstructionTag::kFade:
      ::new (&fade) Fade(*static_cast<const Fade*>(instr));
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
    case InstructionTag::kFade:
      return &fade;
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
    case InstructionTag::kFade:
      return &fade;
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      return &test_timed;
#endif
  }
  return nullptr;
}

std::uint32_t prism::InstructionMemorySlot::Execute(std::uint32_t timestamp) {
  ControllerInstruction* instr = Active();
  return instr != nullptr ? instr->Execute(timestamp) : 0U;
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
    case InstructionTag::kFade:
      fade.~Fade();
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
