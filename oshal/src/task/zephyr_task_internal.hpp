#ifndef OSHAL_ZEPHYR_TASK_INTERNAL_HPP_
#define OSHAL_ZEPHYR_TASK_INTERNAL_HPP_

#include <zephyr/kernel.h>

#include <cstddef>
#include <cstdint>

#include "oshal/task.hpp"

namespace oshal::internal {

/// @brief Number of statically reserved task slots managed by the Zephyr
/// backend.
/// @note This is intentionally limited to a single slot to keep the static SRAM
/// footprint predictable and minimal on current targets. Increase this value
/// only if the system must support more than one concurrent OSHAL-managed task
/// and the additional per-slot RAM cost is acceptable for the board's SRAM
/// budget.
inline constexpr std::size_t kTaskPoolSize = 1U;

/// @brief Maximum stack size, in bytes, accepted for an OSHAL-managed task.
/// @note The 1536-byte limit is a conservative upper bound chosen to balance
/// task stack headroom against tight SRAM constraints. Adjust this value only
/// after measuring Zephyr thread stack usage for the real workload on the
/// target: increase it if valid tasks approach exhaustion, or reduce it if SRAM
/// pressure requires a smaller cap and profiling confirms adequate safety
/// margin.
inline constexpr std::size_t kTaskMaxStackSizeBytes = 1536U;
inline constexpr std::uint8_t kInvalidTaskSlotIndex = UINT8_MAX;

static_assert(
  kTaskRuntimeNameCapacity > 0U,
  "Task runtime names must reserve space for a trailing null terminator.");

struct TaskSlot {
  k_spinlock lock;
  k_thread thread;
  TaskEntry entry;
  void* context;
  int last_exit_code;
  uint8_t generation;
  std::size_t configured_stack_size_bytes;
  TaskRuntimeSample previous_runtime_sample;
  bool have_previous_runtime_sample;
  bool allocated;
  bool running;
  bool exited;
  char name[kTaskRuntimeNameCapacity];
};

extern TaskSlot g_task_slots[kTaskPoolSize];

bool is_valid_task_slot_index(std::uint8_t slot_index);
TaskSlot* find_task_slot(std::uint8_t slot_index, std::uint8_t generation);

}  // namespace oshal::internal

#endif /* OSHAL_ZEPHYR_TASK_INTERNAL_HPP_ */