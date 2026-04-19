#include <zephyr/sys/util.h>

#if defined(CONFIG_ARM)
#include <cmsis_core.h>
#include <zephyr/arch/arm/thread.h>
#endif

#include <cstring>

#include "oshal/status.h"
#include "task/zephyr_task_internal.hpp"

namespace {

constexpr std::size_t kSnapshotRetryLimit = 2U;

struct TaskRuntimeLoad {
  std::uint8_t cpu_runtime_percent = 0U;
  std::uint64_t task_execution_cycles_delta = 0U;
  std::uint64_t system_execution_cycles_delta = 0U;
};

struct TaskSlotSnapshot {
  char name[oshal::kTaskRuntimeNameCapacity] = {};
  bool running = false;
  bool exited = false;
  std::size_t configured_stack_size_bytes = 0U;
  k_tid_t thread_id = nullptr;
  std::uint8_t generation = 0U;
};

/* Start each snapshot from a clean zeroed state so unsupported fields stay
 * explicit. */
void clear_runtime_info(oshal::TaskRuntimeInfo* info) {
  if (info == nullptr) {
    return;
  }

  *info = oshal::TaskRuntimeInfo();
}

/*
 * CPU runtime percentages are derived later from two snapshots, so this helper
 * only captures the cumulative task and system counters that Zephyr maintains.
 */
int copy_runtime_sample(oshal::TaskRuntimeSample* out_sample,
                        k_tid_t thread_id) {
  if (out_sample == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

#if defined(CONFIG_THREAD_RUNTIME_STATS)
  k_thread_runtime_stats_t thread_stats = {};
  k_thread_runtime_stats_t system_stats = {};

  if (k_thread_runtime_stats_get(thread_id, &thread_stats) != 0) {
    return STATUS_ERR_BACKEND;
  }

  if (k_thread_runtime_stats_all_get(&system_stats) != 0) {
    return STATUS_ERR_BACKEND;
  }

  out_sample->task_execution_cycles = thread_stats.execution_cycles;
  out_sample->system_execution_cycles = system_stats.execution_cycles;
  return STATUS_OK;
#else
  ARG_UNUSED(thread_id);
  return STATUS_ERR_BACKEND;
#endif
}

/*
 * Zephyr's portable stack API reports watermark-style usage, but the runtime
 * API also wants a best-effort live value for this Cortex-M backend. For a
 * swapped-out thread the saved PSP in the thread context is the best available
 * estimate, while the current thread must read the live processor PSP instead.
 * The result is intentionally optional because the calculation depends on ARM-
 * specific thread context details and must stay bounded by the tracked writable
 * stack region Zephyr recorded for the thread.
 */
bool try_get_current_stack_used_bytes(k_tid_t thread_id,
                                      std::size_t* out_bytes) {
  if (out_bytes == nullptr) {
    return false;
  }

#if defined(CONFIG_ARM) && defined(CONFIG_THREAD_STACK_INFO)
  const struct k_thread* const thread = thread_id;
  const std::size_t tracked_stack_size = thread->stack_info.size;
  const uintptr_t tracked_stack_start = thread->stack_info.start;
  const uintptr_t initial_stack_pointer =
    tracked_stack_start + tracked_stack_size - thread->stack_info.delta;
  uintptr_t current_stack_pointer =
    static_cast<uintptr_t>(thread->callee_saved.psp);

  if (thread_id == k_current_get()) {
    current_stack_pointer = static_cast<uintptr_t>(__get_PSP());
  }

  if ((current_stack_pointer < tracked_stack_start) ||
      (current_stack_pointer > initial_stack_pointer)) {
    return false;
  }

  *out_bytes =
    static_cast<std::size_t>(initial_stack_pointer - current_stack_pointer);
  return true;
#else
  ARG_UNUSED(thread_id);
  return false;
#endif
}

int calculate_runtime_load(TaskRuntimeLoad* out_load,
                           const oshal::TaskRuntimeSample& previous,
                           const oshal::TaskRuntimeSample& current) {
  if (out_load == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if ((current.task_execution_cycles < previous.task_execution_cycles) ||
      (current.system_execution_cycles < previous.system_execution_cycles)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  const std::uint64_t task_delta =
    current.task_execution_cycles - previous.task_execution_cycles;
  const std::uint64_t system_delta =
    current.system_execution_cycles - previous.system_execution_cycles;

  if (system_delta == 0U) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  out_load->task_execution_cycles_delta = task_delta;
  out_load->system_execution_cycles_delta = system_delta;

  std::uint64_t percent = (task_delta * 100U) / system_delta;
  if (percent > 100U) {
    percent = 100U;
  }
  out_load->cpu_runtime_percent = static_cast<std::uint8_t>(percent);
  return STATUS_OK;
}

bool capture_task_slot_snapshot(oshal::internal::TaskSlot& slot,
                                TaskSlotSnapshot* out_snapshot) {
  if (out_snapshot == nullptr) {
    return false;
  }

  k_spinlock_key_t key = k_spin_lock(&slot.lock);
  if (!slot.allocated) {
    k_spin_unlock(&slot.lock, key);
    return false;
  }

  std::memcpy(out_snapshot->name, slot.name, sizeof(out_snapshot->name));
  out_snapshot->running = slot.running;
  out_snapshot->exited = slot.exited;
  out_snapshot->configured_stack_size_bytes = slot.configured_stack_size_bytes;
  out_snapshot->thread_id = const_cast<k_tid_t>(&slot.thread);
  out_snapshot->generation = slot.generation;
  k_spin_unlock(&slot.lock, key);
  return true;
}

int finalize_runtime_snapshot(oshal::internal::TaskSlot& slot,
                              const TaskSlotSnapshot& snapshot,
                              const oshal::TaskRuntimeInfo& current_info,
                              bool update_previous_sample,
                              oshal::TaskRuntimeSample* out_previous_sample,
                              bool* out_have_previous_sample) {
  k_spinlock_key_t key = k_spin_lock(&slot.lock);
  const bool still_matches =
    slot.allocated && (slot.generation == snapshot.generation);
  if (!still_matches) {
    k_spin_unlock(&slot.lock, key);
    return STATUS_ERR_NOT_READY;
  }

  if (update_previous_sample) {
    if ((out_previous_sample == nullptr) ||
        (out_have_previous_sample == nullptr)) {
      k_spin_unlock(&slot.lock, key);
      return STATUS_ERR_INVALID_ARGUMENT;
    }

    *out_previous_sample = slot.previous_runtime_sample;
    *out_have_previous_sample = slot.have_previous_runtime_sample;
    slot.previous_runtime_sample = current_info.runtime_sample;
    slot.have_previous_runtime_sample = true;
  }

  k_spin_unlock(&slot.lock, key);
  return STATUS_OK;
}

int capture_runtime_info(oshal::TaskRuntimeInfo* out_info,
                         oshal::internal::TaskSlot& slot,
                         bool update_previous_sample) {
  if (out_info == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t attempt = 0U; attempt < kSnapshotRetryLimit; ++attempt) {
    TaskSlotSnapshot snapshot = {};
    if (!capture_task_slot_snapshot(slot, &snapshot)) {
      return STATUS_ERR_NOT_READY;
    }

    clear_runtime_info(out_info);
    std::memcpy(out_info->name, snapshot.name, sizeof(out_info->name));
    out_info->running = snapshot.running;
    out_info->exited = snapshot.exited;
    out_info->configured_stack_size_bytes =
      snapshot.configured_stack_size_bytes;

#if defined(CONFIG_THREAD_STACK_INFO) && defined(CONFIG_INIT_STACKS)
    const struct k_thread* const thread = snapshot.thread_id;
    out_info->tracked_stack_size_bytes = thread->stack_info.size;
    std::size_t unused_stack_bytes = 0U;
    if (k_thread_stack_space_get(snapshot.thread_id, &unused_stack_bytes) !=
        0) {
      return STATUS_ERR_BACKEND;
    }

    out_info->unused_stack_bytes = unused_stack_bytes;
    out_info->high_water_stack_used_bytes =
      (out_info->tracked_stack_size_bytes >= unused_stack_bytes)
        ? (out_info->tracked_stack_size_bytes - unused_stack_bytes)
        : 0U;
#else
    return STATUS_ERR_BACKEND;
#endif

    std::size_t current_stack_used_bytes = 0U;
    if (try_get_current_stack_used_bytes(snapshot.thread_id,
                                         &current_stack_used_bytes)) {
      out_info->current_stack_used_bytes = current_stack_used_bytes;
    }

    const int sample_ret =
      copy_runtime_sample(&out_info->runtime_sample, snapshot.thread_id);
    if (sample_ret < 0) {
      return sample_ret;
    }

    oshal::TaskRuntimeSample previous_sample = {};
    bool have_previous_sample = false;
    const int finalize_ret = finalize_runtime_snapshot(
      slot, snapshot, *out_info, update_previous_sample, &previous_sample,
      &have_previous_sample);
    if (finalize_ret == STATUS_ERR_NOT_READY) {
      continue;
    }
    if (finalize_ret < 0) {
      return finalize_ret;
    }

    if (update_previous_sample && have_previous_sample) {
      TaskRuntimeLoad runtime_load = {};
      if (calculate_runtime_load(&runtime_load, previous_sample,
                                 out_info->runtime_sample) == STATUS_OK) {
        out_info->cpu_runtime_percent = runtime_load.cpu_runtime_percent;
      }
    }

    return STATUS_OK;
  }

  return STATUS_ERR_NOT_READY;
}

}  // namespace

namespace oshal {

int TaskHandle::runtime_info(TaskRuntimeInfo* out_info) const {
  if (out_info == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  internal::TaskSlot* const slot =
    internal::find_task_slot(slot_index_, generation_);

  if (slot == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  return capture_runtime_info(out_info, *slot, true);
}

int snapshot_tasks(TaskRuntimeInfo* out_tasks, std::size_t max_tasks,
                   std::size_t* out_task_count) {
  if (out_task_count == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if ((out_tasks == nullptr) && (max_tasks != 0U)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  *out_task_count = 0U;

  std::size_t allocated_count = 0U;
  for (std::size_t index = 0; index < internal::kTaskPoolSize; ++index) {
    internal::TaskSlot& slot = internal::g_task_slots[index];
    k_spinlock_key_t key = k_spin_lock(&slot.lock);
    const bool allocated = slot.allocated;
    k_spin_unlock(&slot.lock, key);

    if (!allocated) {
      continue;
    }

    ++allocated_count;
  }

  if (out_tasks == nullptr) {
    *out_task_count = allocated_count;
    return STATUS_OK;
  }

  if (allocated_count > max_tasks) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if (allocated_count == 0U) {
    return STATUS_OK;
  }

  std::size_t out_index = 0U;
  for (std::size_t index = 0; index < internal::kTaskPoolSize; ++index) {
    internal::TaskSlot& slot = internal::g_task_slots[index];
    const int ret = capture_runtime_info(&out_tasks[out_index], slot, true);
    if (ret == STATUS_ERR_NOT_READY) {
      continue;
    }
    if (ret < 0) {
      return ret;
    }
    ++out_index;
  }

  *out_task_count = out_index;

  return STATUS_OK;
}

}  // namespace oshal