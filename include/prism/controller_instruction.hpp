#ifndef PRISM_CONTROLLER_INSTRUCTION_HPP_
#define PRISM_CONTROLLER_INSTRUCTION_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/instruction_types.hpp"
#include "prism/strip.hpp"

namespace prism {

class Controller;

/// @brief Polymorphic base for a single queued controller instruction.
class ControllerInstruction {
 public:
  ControllerInstruction(const ControllerInstruction&);
  ControllerInstruction& operator=(const ControllerInstruction&) = delete;
  virtual ~ControllerInstruction();

  /// @brief Execute this instruction against its bound strip.
  /// @param timestamp Timestamp shared by instructions in this controller
  ///     progress tick, in milliseconds.
  /// @return 0 if completed, or a positive duration in milliseconds until the
  ///     next call to Execute(timestamp). Positive durations must be less than
  ///     half the uint32 timestamp range.
  virtual std::uint32_t Execute(std::uint32_t timestamp) = 0;

  /// @brief Return the tag identifying this instruction's concrete type.
  /// @return InstructionTag value set by the derived-class constructor.
  InstructionTag Tag() const;

  /// @brief Write a human-readable description of this instruction into
  ///     a caller-provided buffer.
  /// @param buf  Destination buffer.
  /// @param size Buffer capacity.
  virtual void ToString(char* buf, std::size_t size) const;

  /// @brief Reset transient execution state before replaying this instruction.
  ///
  /// Configuration copied into the fixed instruction slot remains unchanged.
  /// Stateful instructions override this hook to clear completion or timing
  /// state before a new group activation.
  virtual void ResetForReplay();

  /// @brief Report whether this instruction provides a positive time boundary.
  ///
  /// The controller uses this static capability during pre-run validation of
  /// repeated groups.  Instant instructions return false.
  /// @return True when a future deadline can bound a repeated pass.
  virtual bool HasPositiveSchedulingBoundary() const;

  /// @brief Report whether this instruction starts a child group.
  /// @return True only for RunGroupInstruction.
  virtual bool IsRunGroup() const;

  /// @brief Return the target group label for a RunGroup instruction.
  /// @return Target GroupId, or zero for other instruction types.
  virtual GroupId TargetGroupId() const;

  /// @brief Return the child repeat count for a RunGroup instruction.
  /// @return Additional repeats, or zero for other instruction types.
  virtual LoopCount AdditionalRepeats() const;

 protected:
  ControllerInstruction();
  explicit ControllerInstruction(Mark m);
  InstructionTag tag_{};

 public:
  /// @brief Offset from the owning group's local activation time.
  Mark mark{0U};
  /// @brief Non-owning pointer to the target strip, or nullptr.
  Strip* strip{nullptr};
  /// @brief Non-owning pointer to the owning controller, or nullptr.
  ///     Set by Controller::AddInstruction.
  Controller* controller{nullptr};
  /// @brief Group label owning this instruction.  Defaults to the root group.
  GroupId group_id{0U};
};

}  // namespace prism

#endif  // PRISM_CONTROLLER_INSTRUCTION_HPP_
