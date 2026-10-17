#include "prism/controller_instruction.hpp"

#include <cstdio>

#include "prism/instruction_types.hpp"

prism::ControllerInstruction::ControllerInstruction() = default;

prism::ControllerInstruction::ControllerInstruction(prism::Mark m) : mark{m} {}

prism::ControllerInstruction::ControllerInstruction(
  const prism::ControllerInstruction&) = default;

prism::ControllerInstruction::~ControllerInstruction() = default;

prism::InstructionTag prism::ControllerInstruction::Tag() const { return tag_; }

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
    case InstructionTag::kFade:
      return "Fade";
#if defined(PRISM_CONTROLLER_TESTING)
    case InstructionTag::kTestTimed:
      return "TimedInstruction";
#endif
  }
  return "Unknown";
}

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

void prism::ControllerInstruction::ToString(char* buf, std::size_t size) const {
  std::snprintf(buf, size, "%s", prism::InstructionToString(tag_));
}
