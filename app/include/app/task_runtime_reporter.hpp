#ifndef APP_TASK_RUNTIME_REPORTER_HPP_
#define APP_TASK_RUNTIME_REPORTER_HPP_

#include "oshal/task.hpp"

namespace app {

/// @brief Capture and print runtime diagnostics for one OSHAL task handle.
/// @note This helper is an APP-layer example of consuming the OSHAL task
///     runtime snapshot API without depending on Zephyr-specific types.
class TaskRuntimeReporter {
 public:
  /// @brief Construct a reporter for one OSHAL task.
  /// @param task_handle Handle of the task to sample.
  /// @pre @p task_handle should remain valid for the runtime of the monitored
  ///     task.
  explicit TaskRuntimeReporter(const oshal::TaskHandle& task_handle);

  /// @brief Capture and print one runtime sample for the configured task.
  /// @return STATUS_OK on success, STATUS_ERR_NOT_READY if the task is not
  ///     present in the current snapshot, or another negative project-defined
  ///     status code when the snapshot or formatted output fails.
  /// @note CPU runtime or live stack usage values print as `n/a` when OSHAL
  ///     marks them unavailable in the returned task snapshot.
  int report();

 private:
  oshal::TaskHandle task_handle_;
};

}  // namespace app

#endif /* APP_TASK_RUNTIME_REPORTER_HPP_ */