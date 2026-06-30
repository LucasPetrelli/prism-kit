#ifndef APP_HW_HW_TASK_HPP_
#define APP_HW_HW_TASK_HPP_

#include "oshal/event.hpp"
#include "oshal/task.hpp"

namespace app::hw {

/// @brief Owns the app_hw task handle and the event-flag group the task
///     sleeps on.
///
/// HwTask wraps an oshal::TaskHandle together with the
/// oshal::EventFlagGroup that drives the task's event loop.  Producers
/// (StripManager's mailbox, CommandManager's UART RX) post bits to this
/// event group; the coordinator calls WaitAny on it inside the task loop.
class HwTask {
 public:
  /// @brief Construct with a reference to the externally-owned wake event
  ///     group.
  /// @param event_group EventFlagGroup that the task loop waits on.
  /// @pre event_group must outlive this HwTask.
  explicit HwTask(oshal::EventFlagGroup& event_group);

  /// @brief Create and start the underlying Zephyr task.
  /// @param name      Diagnostic task name.
  /// @param setup     C-callable setup trampoline (may be null).
  /// @param loop      C-callable loop trampoline (must not be null).
  /// @param context   Opaque pointer passed to setup and loop.
  /// @param stack_size_bytes Stack size in bytes.
  /// @param priority  Task priority (lower = higher).
  /// @return STATUS_OK on success, or a negative status code.
  /// @pre loop must not be null.
  int Start(const char* name, oshal::TaskSetup setup, oshal::TaskLoop loop,
            void* context, std::size_t stack_size_bytes, int priority);

  /// @brief Query whether the underlying task handle is valid.
  /// @return true when the task has been created.
  bool IsRunning() const;

  /// @brief Query whether the task has exited.
  /// @return true when the task has terminated.
  bool HasExited() const;

  /// @brief Retrieve the task exit code, if available.
  /// @param[out] out_code Receives the exit code.
  /// @return STATUS_OK on success, or a negative status code.
  int ExitCode(int* out_code) const;

  /// @brief Access the event-flag group the task loop blocks on.
  /// @return Reference to the wake event group.
  oshal::EventFlagGroup& EventGroup() { return event_group_; }

 private:
  oshal::EventFlagGroup& event_group_;
  oshal::TaskHandle task_;
};

}  // namespace app::hw

#endif /* APP_HW_HW_TASK_HPP_ */
