#ifndef OSHAL_TASK_HPP_
#define OSHAL_TASK_HPP_

#include <cstdint>
#include <cstddef>

namespace oshal {

/// @brief Stable C++ task-entry signature owned by OSHAL.
/// @param context Optional caller-supplied task context.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     the task exits because of a failure.
/// @note Task entries run in Zephyr thread context created by the active OSHAL
///     backend. They are not ISR-safe entry points.
using TaskEntry = int (*)(void *context);

class TaskHandle;

/// @brief Task creation parameters interpreted by the active OSHAL backend.
/// @note Callers are expected to keep the pointed-to task name and context
///     valid for the lifetime of the started task.
struct TaskConfig {
	/// @brief Optional task name for diagnostics.
	const char *name = nullptr;
	/// @brief Entry function to execute in task context.
	TaskEntry entry = nullptr;
	/// @brief Optional caller-owned context passed to @p entry.
	void *context = nullptr;
	/// @brief Requested stack size in bytes.
	/// @note The active backend may reject requests that exceed its statically
	///     reserved stack storage budget.
	std::size_t stack_size_bytes = 0U;
	/// @brief Backend-defined task priority.
	/// @note Priority values follow the semantics of the active OSHAL backend.
	int priority = 0;
};

/// @brief Lightweight C++ handle for a running or completed OSHAL task.
/// @note A default-constructed handle is invalid until start() stores a task
///     slot reference into it.
class TaskHandle {
public:
	/// @brief Construct an invalid task handle sentinel.
	TaskHandle();

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
	int exit_code(int *out_exit_code) const;

	/// @brief Release this task handle for slot reuse.
	/// @return STATUS_OK on success, STATUS_ERR_NOT_READY when the task is still
	///     running, or STATUS_ERR_INVALID_ARGUMENT when the handle is invalid.
	/// @post On success, this handle becomes invalid again.
	int release();

private:
	friend int start(TaskHandle &handle, const TaskConfig &config);

	std::uint8_t slot_index_;
	std::uint8_t generation_;
};

/// @brief Create and schedule a task through the active OSHAL backend.
/// @param handle Receives the created task handle on success.
/// @param config Task configuration owned by the caller.
/// @return STATUS_OK on success, STATUS_ERR_INVALID_ARGUMENT when the
///     configuration is invalid, STATUS_ERR_BACKEND when the backend rejects the
///     Zephyr thread creation request, or STATUS_ERR_DEVICE_UNAVAILABLE when no
///     backend task slot is available.
/// @pre @p config.entry must not be null.
/// @pre @p config.stack_size_bytes must be non-zero and within the backend's
///     supported stack budget.
/// @post On success, @p handle references the started task and may be queried
///     with is_running(), has_exited(), exit_code(), and release().
int start(TaskHandle &handle, const TaskConfig &config);

} // namespace oshal

#endif /* OSHAL_TASK_HPP_ */