#include "prism/pending_instruction_queue_view.hpp"

#include <cstdint>

#include "prism/instruction_memory_slot.hpp"
#include "prism/instruction_types.hpp"

void prism::PendingInstructionQueueView::SetInstructions(
  InstructionMemorySlot* instructions) {
  instructions_ = instructions;
}

bool prism::PendingInstructionQueueView::HasNext() const {
  return head_ < sorted_count_;
}

std::uint32_t prism::PendingInstructionQueueView::Peek() const {
  return sorted_order_[head_];
}

prism::Mark prism::PendingInstructionQueueView::PeekMark() const {
  return instructions_[sorted_order_[head_]].GetMark();
}

void prism::PendingInstructionQueueView::Insert(std::uint32_t slot_idx) {
  // Insertion-sort by ascending mark. Linear scan is fine because the count
  // is capped at kCapacity.
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

void prism::PendingInstructionQueueView::Advance() { ++head_; }

void prism::PendingInstructionQueueView::Reset() {
  sorted_count_ = 0U;
  head_ = 0U;
}
