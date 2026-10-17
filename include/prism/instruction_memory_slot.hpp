#ifndef PRISM_INSTRUCTION_MEMORY_SLOT_HPP_
#define PRISM_INSTRUCTION_MEMORY_SLOT_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/fade.hpp"
#include "prism/instruction_types.hpp"
#include "prism/run_group_instruction.hpp"
#include "prism/set_multiple_color.hpp"
#include "prism/set_multiple_color_hsv.hpp"
#include "prism/set_single_color.hpp"
#include "prism/set_single_color_hsv.hpp"
#if defined(PRISM_CONTROLLER_TESTING)
#include "prism/test/timed_instruction.hpp"
#endif

namespace prism {

/// @brief Variant storage for one instruction slot with active-member tracking.
///
/// Tracks which member is active via tag_ so that set(), execute() and
/// the destructor always operate on the correct type.
struct InstructionMemorySlot {
  union {
    /// @brief Active member: range-fill instruction.
    SetMultipleColor set_multiple_color;
    /// @brief Active member: single-pixel instruction.
    SetSingleColor set_single_color;
    /// @brief Active member: HSV range-fill instruction.
    SetMultipleColorHsv set_multiple_color_hsv;
    /// @brief Active member: HSV single-pixel instruction.
    SetSingleColorHsv set_single_color_hsv;
    /// @brief Active member: child-group activation instruction.
    RunGroupInstruction run_group;
    /// @brief Active member: one-pixel Fade instruction.
    Fade fade;
#if defined(PRISM_CONTROLLER_TESTING)
    /// @brief Active member: host-test timed instruction.
    test::TimedInstruction test_timed;
#endif
  };

  /// @brief Tag identifying the currently-active member.
  InstructionTag tag{};

  /// @brief Default constructor — leaves storage uninitialised and inactive.
  InstructionMemorySlot();

  /// @brief Destroy the active member before the slot goes out of scope.
  ~InstructionMemorySlot();

  InstructionMemorySlot(const InstructionMemorySlot&) = delete;
  InstructionMemorySlot& operator=(const InstructionMemorySlot&) = delete;

  /// @brief Activate this slot with a copy of an already-constructed
  ///     instruction.  The previously-active member is destroyed first.
  /// @param instr Pointer to the source instruction.  Must not be null.
  /// @return True when the instruction tag was stored.
  bool Set(const ControllerInstruction* instr);

  /// @brief Destroy the active instruction and leave the slot empty.
  void Clear();

  /// @brief Set the strip pointer on the active instruction.
  /// @param s Non-owning pointer to the strip to bind.
  void SetStrip(Strip* s);

  /// @brief Execute the active instruction.
  /// @param timestamp Progress-tick timestamp forwarded to the active
  ///     instruction.
  /// @return 0 if completed, or a positive duration in ms until the next call.
  std::uint32_t Execute(std::uint32_t timestamp);

  /// @brief Write a description of the active instruction into a buffer.
  /// @param buf  Destination buffer.
  /// @param size Buffer capacity.
  void ToString(char* buf, std::size_t size) const;

  /// @brief Return the mark of the active instruction.
  /// @return The mark value, or 0 if no instruction is active.
  Mark GetMark() const;

  /// @brief Set the controller pointer on the active instruction.
  /// @param c Non-owning pointer to the owning controller.
  void SetController(Controller* c);

  /// @brief Return the owning group label.
  /// @return GroupId, or zero when the slot is empty.
  GroupId GetGroupId() const;

  /// @brief Return whether the active instruction is a RunGroup.
  /// @return True for a child-group activation.
  bool IsRunGroup() const;

  /// @brief Return the RunGroup target label.
  /// @return Target GroupId, or zero for other instructions.
  GroupId GetTargetGroupId() const;

  /// @brief Return the RunGroup repeat count.
  /// @return LoopCount, or zero for other instructions.
  LoopCount GetAdditionalRepeats() const;

  /// @brief Return whether this slot offers a positive static time boundary.
  /// @return True when the active instruction can bound a repeated pass.
  bool HasPositiveSchedulingBoundary() const;

  /// @brief Reset transient state in the active instruction for replay.
  void ResetForReplay();

 private:
  /// @brief Return a base-class pointer to the active instruction.
  ControllerInstruction* Active();

  /// @brief Return a const base-class pointer to the active instruction.
  const ControllerInstruction* Active() const;

  /// @brief Destroy the active member and reset the tag.
  void Destroy();

  /// @brief True once a concrete union member has been constructed.
  bool active_;
};

}  // namespace prism

#endif  // PRISM_INSTRUCTION_MEMORY_SLOT_HPP_
