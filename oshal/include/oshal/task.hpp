#ifndef OSHAL_TASK_HPP_
#define OSHAL_TASK_HPP_

#include <cstddef>
#include <cstdint>
#include <limits>

namespace oshal {

/// @brief Maximum task name length copied into runtime snapshots.
/// @note This count includes the trailing null terminator when a runtime name
///     is present.
constexpr std::size_t kTaskRuntimeNameCapacity = 24U;

/// @brief Sentinel returned when interval CPU runtime is not available.
constexpr std::uint32_t kTaskRuntimePercentUnavailable =
  std::numeric_limits<std::uint32_t>::max();

/// @brief Sentinel returned when a best-effort stack usage value is not
/// available.
constexpr std::size_t kTaskStackUsageUnavailable =
  std::numeric_limits<std::size_t>::max();

/// @brief Maximum stack size, in bytes, accepted for an OSHAL-managed task.
/// @note The active backend reserves storage for at most this many bytes per
///     OSHAL-managed task. Higher layers should size tasks within this budget.
constexpr std::size_t kTaskMaxStackSizeBytes = 1536U;

/// @brief Stable C++ task-entry signature owned by OSHAL.
/// @param context Optional caller-supplied task context.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     the task exits because of a failure.
/// @note Task entries run in Zephyr thread context created by the active OSHAL
///     backend. They are not ISR-safe entry points.
using TaskEntry = int (*)(void* context);

class TaskHandle;

/// @brief Cumulative CPU runtime counters sampled for one task.
/// @note These counters are monotonically increasing while the task and system
///     execute. OSHAL uses successive samples internally to derive the
///     interval CPU percentage reported in TaskRuntimeInfo.
struct TaskRuntimeSample {
  /// @brief Total execution cycles accumulated by the sampled task.
  std::uint64_t task_execution_cycles = 0U;
  /// @brief Total execution cycles accumulated by the system.
  /// @note This denominator follows the active backend's runtime-stat
  /// semantics.
  std::uint64_t system_execution_cycles = 0U;
};

/// @brief Snapshot of runtime information for one OSHAL task.
/// @note Stack usage metrics refer to the backend-reported writable stack area
///     tracked at runtime. That tracked size may differ from the caller's
///     configured stack request when the backend reserves guard or TLS space.
struct TaskRuntimeInfo {
  /// @brief Null-terminated diagnostic task name copied into the snapshot.
  char name[kTaskRuntimeNameCapacity] = {};
  /// @brief True while the task is still scheduled or runnable.
  bool running = false;
  /// @brief True once the task has returned from its entry function.
  bool exited = false;
  /// @brief Caller-requested stack size from TaskConfig.
  std::size_t configured_stack_size_bytes = 0U;
  /// @brief Backend-tracked writable stack region used for stack metrics.
  std::size_t tracked_stack_size_bytes = 0U;
  /// @brief Unused bytes remaining in the tracked stack region.
  std::size_t unused_stack_bytes = 0U;
  /// @brief Highest observed stack usage in bytes.
  /// @note This follows Zephyr's stack-space inspection semantics and reflects
  ///     watermark-style usage over the task lifetime.
  std::size_t high_water_stack_used_bytes = 0U;
  /// @brief Best-effort current live stack usage in bytes.
  /// @note When a live estimate is unavailable, this field is set to
  ///     kTaskStackUsageUnavailable.
  std::size_t current_stack_used_bytes = kTaskStackUsageUnavailable;
  /// @brief Interval CPU runtime percentage for this task.
  /// @note When an interval percentage is unavailable, such as the first sample
  ///     or a backend that cannot derive it, this field is set to
  ///     kTaskRuntimePercentUnavailable.
  std::uint32_t cpu_runtime_percent = kTaskRuntimePercentUnavailable;
  /// @brief Cumulative CPU runtime counters for interval-based load sampling.
  TaskRuntimeSample runtime_sample = {};
};

/// @brief Task creation parameters interpreted by the active OSHAL backend.
/// @note The pointed-to task context must remain valid for the lifetime of the
///     started task. The backend copies the optional task name during task
///     creation, so the name pointer only needs to remain valid for the call.
struct TaskConfig {
  /// @brief Optional task name for diagnostics.
  const char* name = nullptr;
  /// @brief Entry function to execute in task context.
  TaskEntry entry = nullptr;
  /// @brief Optional caller-owned context passed to @p entry.
  void* context = nullptr;
  /// @brief Requested stack size in bytes.
  /// @note The active backend may reject requests that exceed
  ///     kTaskMaxStackSizeBytes.
  std::size_t stack_size_bytes = 0U;
  /// @brief Backend-defined task priority.
  /// @note Priority values follow the semantics of the active OSHAL backend.
  int priority = 0;
};

/// @brief Lightweight C++ handle for a running or completed OSHAL task.
/// @note A default-constructed handle is invalid until create() stores a
///     task-slot reference into it.
class TaskHandle {
 public:
  /// @brief Construct an invalid task handle sentinel.
  TaskHandle();

  /// @brief Create and schedule a task through the active OSHAL backend.
  /// @param handle Receives the created task handle on success.
  /// @param config Task configuration owned by the caller.
  /// @return STATUS_OK on success, STATUS_ERR_INVALID_ARGUMENT when the
  ///     configuration is invalid or @p handle is already valid,
  ///     STATUS_ERR_BACKEND when the backend rejects the Zephyr thread creation
  ///     request, or STATUS_ERR_DEVICE_UNAVAILABLE when no backend task slot is
  ///     available.
  /// @pre @p config.entry must not be null.
  /// @pre @p config.stack_size_bytes must be non-zero and within the backend's
  ///     supported stack budget.
  /// @post On success, @p handle references the started task and may be queried
  ///     with is_running(), has_exited(), exit_code(), and release().
  static int create(TaskHandle& handle, const TaskConfig& config);

  /// @brief Capture a handle for the current OSHAL-owned task.
  /// @return A valid handle for the current task when the calling thread maps
  ///     to an OSHAL-managed slot, otherwise an invalid handle sentinel.
  /// @note This lets higher layers query their own runtime information without
  ///     indexing diagnostics through a task name string.
  static TaskHandle current();

  /// @brief Report whether this handle currently references a task slot.
  /// @return True when the handle is not the invalid sentinel.
  /// @post Returns false for a default-constructed handle and for any handle
  ///     released through release().
  bool is_valid() const;

  /// @brief Report whether the referenced task is still running.
  /// @return True while the task is active, otherwise false.
  /// @note Invalid handles report false.
  bool is_running() const;

  /// @brief Report whether the referenced task has exited.
  /// @return True when the task completed, otherwise false.
  /// @note Invalid handles report false.
  bool has_exited() const;

  /// @brief Retrieve the task exit code if the task already completed.
  /// @param out_exit_code Receives the recorded task exit code.
  /// @return STATUS_OK on success, STATUS_ERR_NOT_READY when the task is still
  ///     running, or STATUS_ERR_INVALID_ARGUMENT when the handle or output
  ///     pointer is invalid.
  /// @pre @p out_exit_code must not be null.
  int exit_code(int* out_exit_code) const;

  /// @brief Capture a runtime diagnostics snapshot for the referenced task.
  /// @param out_info Receives the copied runtime information.
  /// @return STATUS_OK on success, STATUS_ERR_INVALID_ARGUMENT when the handle
  ///     or output pointer is invalid, or STATUS_ERR_NOT_READY when the task
  ///     disappears while the backend is collecting a coherent snapshot.
  /// @pre @p out_info must not be null.
  /// @note The returned snapshot already includes an interval CPU percentage in
  ///     TaskRuntimeInfo::cpu_runtime_percent when one is available.
  ///     TaskRuntimeInfo::runtime_sample still exposes the raw cumulative
  ///     counters for callers that need them. The interval CPU percentage is
  ///     derived against the previous OSHAL runtime sample captured for this
  ///     task, regardless of which runtime query path captured that sample.
  int runtime_info(TaskRuntimeInfo* out_info) const;

  /// @brief Release this task handle for slot reuse.
  /// @return STATUS_OK on success, STATUS_ERR_NOT_READY when the task is still
  ///     running, or STATUS_ERR_INVALID_ARGUMENT when the handle is invalid.
  /// @post On success, this handle becomes invalid again.
  int release();

 private:
  std::uint8_t slot_index_;
  std::uint8_t generation_;
};

/// @brief Report the maximum number of concurrent OSHAL tasks supported.
/// @return Number of task slots reserved by the active backend.
std::size_t task_capacity();

/// @brief Capture runtime snapshots for every currently allocated OSHAL task.
/// @param out_tasks Caller-owned array receiving copied task snapshots.
/// @param max_tasks Number of elements available in @p out_tasks.
/// @param out_task_count Receives the number of coherent task snapshots copied,
///     or the number of currently allocated tasks when @p out_tasks is null.
/// @return STATUS_OK on success or STATUS_ERR_INVALID_ARGUMENT when
///     @p out_task_count is null, when @p out_tasks is null and @p max_tasks is
///     non-zero, or when @p max_tasks is smaller than the number of allocated
///     tasks.
/// @note Callers may pass @p out_tasks as null with @p max_tasks equal to zero
///     to query only the current task count.
/// @note Each copied snapshot uses the same interval CPU percentage semantics
///     as TaskHandle::runtime_info().
int snapshot_tasks(TaskRuntimeInfo* out_tasks, std::size_t max_tasks,
                   std::size_t* out_task_count);

}  // namespace oshal

#endif /* OSHAL_TASK_HPP_ */