#include <zephyr/sys/util.h>

#include <cstring>

#include "oshal/status.h"
#include "task/zephyr_task_internal.hpp"

namespace {

/*
 * The current backend keeps a tiny static task pool because the XIAO SAMD21
 * target has only 32 KiB of SRAM and this firmware currently launches one APP
 * task. This array reserves kTaskPoolSize × kTaskMaxStackSizeBytes bytes of
 * static stack storage, which is 1 × 1536 = 1536 bytes with the current
 * settings.
 * The public API does not expose this storage strategy so the backend can
 * evolve later without forcing a caller-visible contract change. Zephyr's
 * K_THREAD_STACK_ARRAY_DEFINE() macro provides the required thread-stack
 * object layout and architecture-specific alignment for each stack, so this
 * storage must be treated as Zephyr-managed stack memory rather than a plain
 * byte array.
 */
K_THREAD_STACK_ARRAY_DEFINE(g_task_stacks, oshal::internal::kTaskPoolSize,
                            oshal::internal::kTaskMaxStackSizeBytes);

/*
 * Zephyr thread entry points use the three-argument kernel signature, so the
 * backend keeps this narrow trampoline that forwards into the stored C++ task
 * entry and records the exit status before the thread returns.
 */
void task_main_trampoline(void* parameter1, void*, void*) {
  auto* const slot = static_cast<oshal::internal::TaskSlot*>(parameter1);
  const int exit_code = slot->entry(slot->context);
  k_spinlock_key_t key = k_spin_lock(&slot->lock);

  slot->last_exit_code = exit_code;
  slot->running = false;
  slot->exited = true;
  k_spin_unlock(&slot->lock, key);
}

}  // namespace

namespace oshal::internal {

TaskSlot g_task_slots[kTaskPoolSize] = {};

bool is_valid_task_slot_index(std::uint8_t slot_index) {
  return slot_index != kInvalidTaskSlotIndex;
}

TaskSlot* find_task_slot(std::uint8_t slot_index, std::uint8_t generation) {
  if (slot_index >= kTaskPoolSize) {
    return nullptr;
  }

  TaskSlot& slot = g_task_slots[slot_index];
  k_spinlock_key_t key = k_spin_lock(&slot.lock);
  const bool valid = slot.allocated && (slot.generation == generation);
  k_spin_unlock(&slot.lock, key);

  return valid ? &slot : nullptr;
}

}  // namespace oshal::internal

namespace oshal {

TaskHandle::TaskHandle()
    : slot_index_(internal::kInvalidTaskSlotIndex), generation_(0U) {}

int TaskHandle::create(TaskHandle& handle, const TaskConfig& config) {
  if (handle.is_valid()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if ((config.entry == nullptr) || (config.stack_size_bytes == 0U) ||
      (config.stack_size_bytes > internal::kTaskMaxStackSizeBytes)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  /*
   * The backend allocates from a small static pool so task creation remains
   * deterministic and does not rely on heap allocation in steady-state paths.
   */
  for (std::size_t index = 0; index < internal::kTaskPoolSize; ++index) {
    internal::TaskSlot& slot = internal::g_task_slots[index];
    k_spinlock_key_t key = k_spin_lock(&slot.lock);

    if (slot.allocated) {
      k_spin_unlock(&slot.lock, key);
      continue;
    }

    slot.allocated = true;
    slot.running = true;
    slot.exited = false;
    slot.entry = config.entry;
    slot.context = config.context;
    slot.last_exit_code = STATUS_ERR_NOT_READY;
    slot.configured_stack_size_bytes = config.stack_size_bytes;
    slot.previous_runtime_sample = TaskRuntimeSample();
    slot.have_previous_runtime_sample = false;
    slot.generation = static_cast<uint8_t>(slot.generation + 1U);
    if (slot.generation == 0U) {
      slot.generation = 1U;
    }
    if (config.name != nullptr) {
      std::strncpy(slot.name, config.name, sizeof(slot.name) - 1U);
      slot.name[sizeof(slot.name) - 1U] = '\0';
    } else {
      slot.name[0] = '\0';
    }

    handle.slot_index_ = static_cast<uint8_t>(index);
    handle.generation_ = slot.generation;
    k_spin_unlock(&slot.lock, key);

    k_tid_t const thread_id =
      k_thread_create(&slot.thread, g_task_stacks[index],
                      config.stack_size_bytes, task_main_trampoline, &slot,
                      nullptr, nullptr, config.priority, 0, K_NO_WAIT);

    if (thread_id == nullptr) {
      /* Roll back the reserved slot if Zephyr rejects the thread creation. */
      key = k_spin_lock(&slot.lock);
      slot.allocated = false;
      slot.running = false;
      slot.exited = false;
      slot.configured_stack_size_bytes = 0U;
      slot.previous_runtime_sample = TaskRuntimeSample();
      slot.have_previous_runtime_sample = false;
      k_spin_unlock(&slot.lock, key);
      handle = TaskHandle();
      return STATUS_ERR_BACKEND;
    }

    if ((slot.name[0] != '\0') && IS_ENABLED(CONFIG_THREAD_NAME)) {
      k_thread_name_set(thread_id, slot.name);
    }

    return STATUS_OK;
  }

  return STATUS_ERR_DEVICE_UNAVAILABLE;
}

TaskHandle TaskHandle::current() {
  TaskHandle handle;
  k_tid_t const current_thread = k_current_get();

  for (std::size_t index = 0; index < internal::kTaskPoolSize; ++index) {
    internal::TaskSlot& slot = internal::g_task_slots[index];
    k_spinlock_key_t key = k_spin_lock(&slot.lock);
    const bool matches_current_thread =
      slot.allocated && (const_cast<k_tid_t>(&slot.thread) == current_thread);
    const std::uint8_t generation = slot.generation;
    k_spin_unlock(&slot.lock, key);

    if (!matches_current_thread) {
      continue;
    }

    handle.slot_index_ = static_cast<std::uint8_t>(index);
    handle.generation_ = generation;
    return handle;
  }

  return handle;
}

bool TaskHandle::is_valid() const {
  return internal::is_valid_task_slot_index(slot_index_);
}

bool TaskHandle::is_running() const {
  internal::TaskSlot* const slot =
    internal::find_task_slot(slot_index_, generation_);

  if (slot == nullptr) {
    return false;
  }

  k_spinlock_key_t key = k_spin_lock(&slot->lock);
  const bool running = slot->running;
  k_spin_unlock(&slot->lock, key);
  return running;
}

bool TaskHandle::has_exited() const {
  internal::TaskSlot* const slot =
    internal::find_task_slot(slot_index_, generation_);

  if (slot == nullptr) {
    return false;
  }

  k_spinlock_key_t key = k_spin_lock(&slot->lock);
  const bool exited = slot->exited;
  k_spin_unlock(&slot->lock, key);
  return exited;
}

int TaskHandle::exit_code(int* out_exit_code) const {
  if (out_exit_code == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  internal::TaskSlot* const slot =
    internal::find_task_slot(slot_index_, generation_);

  if (slot == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  k_spinlock_key_t key = k_spin_lock(&slot->lock);

  if (!slot->exited) {
    k_spin_unlock(&slot->lock, key);
    return STATUS_ERR_NOT_READY;
  }

  *out_exit_code = slot->last_exit_code;
  k_spin_unlock(&slot->lock, key);
  return STATUS_OK;
}

int TaskHandle::release() {
  if (!internal::is_valid_task_slot_index(slot_index_)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  internal::TaskSlot* const slot =
    internal::find_task_slot(slot_index_, generation_);

  if (slot == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  k_spinlock_key_t key = k_spin_lock(&slot->lock);

  if (slot->running) {
    k_spin_unlock(&slot->lock, key);
    return STATUS_ERR_NOT_READY;
  }

  /* Clear caller-owned metadata before the slot goes back into the static pool.
   */
  slot->allocated = false;
  slot->exited = false;
  slot->entry = nullptr;
  slot->context = nullptr;
  slot->last_exit_code = STATUS_ERR_NOT_READY;
  slot->configured_stack_size_bytes = 0U;
  slot->previous_runtime_sample = TaskRuntimeSample();
  slot->have_previous_runtime_sample = false;
  slot->name[0] = '\0';
  k_spin_unlock(&slot->lock, key);

  *this = TaskHandle();
  return STATUS_OK;
}

std::size_t task_capacity() { return internal::kTaskPoolSize; }

}  // namespace oshal