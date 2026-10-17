#ifndef PRISM_RUN_GROUP_INSTRUCTION_HPP_
#define PRISM_RUN_GROUP_INSTRUCTION_HPP_

#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Instruction that activates another group's timeline.
class RunGroupInstruction : public ControllerInstruction {
 public:
  /// @brief Construct an empty root-owned group activation.
  RunGroupInstruction();

  /// @brief Construct from a controller RunGroup payload.
  /// @param payload Source payload to unpack.
  explicit RunGroupInstruction(const RunGroupPayload& payload);

  /// @brief Direct execution is a no-op; Controller::Run handles activation.
  /// @param timestamp Timestamp shared by this controller progress tick; this
  ///     instruction is dispatched by the group scheduler instead.
  /// @return 0 when called outside the controller's group scheduler.
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Write a description including target and repeat policy.
  /// @param buf Destination buffer.
  /// @param size Buffer capacity.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief Identify this instruction as a child-group activation.
  /// @return Always true.
  bool IsRunGroup() const override;

  /// @brief Return the requested child group label.
  /// @return Target GroupId.
  GroupId TargetGroupId() const override;

  /// @brief Return the requested additional child repeats.
  /// @return LoopCount repeat count.
  LoopCount AdditionalRepeats() const override;

  /// @brief Nonzero group label to activate.
  GroupId target_id{0U};
  /// @brief Additional child passes, or kForeverLoopCount.
  LoopCount additional_repeats{0U};
};

}  // namespace prism

#endif  // PRISM_RUN_GROUP_INSTRUCTION_HPP_
