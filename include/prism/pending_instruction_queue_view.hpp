#ifndef PRISM_PENDING_INSTRUCTION_QUEUE_VIEW_HPP_
#define PRISM_PENDING_INSTRUCTION_QUEUE_VIEW_HPP_

#include <cstdint>

#include "prism/instruction_memory_slot.hpp"

namespace prism {

/// @brief Read-only view over the Controller's instruction array
///     exposing a mark-sorted iteration cursor (head).  Insertions are
///     O(n) where n = sorted_count_ (capped at kCapacity).
///
/// This class does not own the instruction storage — it only tracks
/// indices into the Controller's ``instructions_[]`` array.
///
/// Usage pattern:
/// @code
///   while (queue.HasNext() && queue.PeekMark() <= now) {
///     uint32_t idx = queue.Peek();
///     /* ... execute instructions_[idx] ... */
///     queue.Advance();
///   }
/// @endcode
class PendingInstructionQueueView {
 public:
  /// @brief Maximum number of entries this view can hold.
  static constexpr std::uint32_t kCapacity = kMaxInstructions;

  /// @brief Set the instruction array for mark lookups.
  /// @param instructions Non-owning pointer to the Controller's
  ///     InstructionMemorySlot array.  Must outlive this queue.
  void SetInstructions(InstructionMemorySlot* instructions);

  /// @brief Insert a slot index in ascending-mark order.
  /// @param slot_idx Index into the Controller's instructions_[] array.
  /// @pre slot_idx < kCapacity.
  void Insert(std::uint32_t slot_idx);

  /// @brief True when at least one unconsumed instruction remains.
  bool HasNext() const;

  /// @brief Index of the next unconsumed instruction.
  /// @pre HasNext() is true.
  std::uint32_t Peek() const;

  /// @brief Mark of the next unconsumed instruction.
  /// @pre HasNext() is true.
  Mark PeekMark() const;

  /// @brief Advance the cursor past the current head.
  /// @pre HasNext() is true.
  void Advance();

  /// @brief Reset cursor and clear all entries.
  void Reset();

 private:
  /// @brief Non-owning pointer to the instruction array for GetMark().
  InstructionMemorySlot* instructions_{nullptr};
  /// @brief Indices into the instruction array, sorted by ascending mark.
  std::uint32_t sorted_order_[kCapacity]{};
  /// @brief Number of valid entries in sorted_order_.
  std::uint32_t sorted_count_{0U};
  /// @brief Index of the next entry to consume.
  std::uint32_t head_{0U};
};

}  // namespace prism

#endif  // PRISM_PENDING_INSTRUCTION_QUEUE_VIEW_HPP_
