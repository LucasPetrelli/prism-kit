#include <cstdint>
#include <cstdio>

#include "prism/controller_instruction.hpp"
#include "prism/instruction_types.hpp"
#include "prism/run_group_instruction.hpp"

prism::RunGroupInstruction::RunGroupInstruction() {
  tag_ = prism::InstructionTag::kRunGroup;
}

prism::RunGroupInstruction::RunGroupInstruction(
  const prism::RunGroupPayload& payload)
    : prism::ControllerInstruction(payload.mark),
      target_id{payload.target_id},
      additional_repeats{payload.additional_repeats} {
  tag_ = prism::InstructionTag::kRunGroup;
  group_id = payload.group_id;
}

std::uint32_t prism::RunGroupInstruction::Execute(std::uint32_t) { return 0U; }

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
