#ifndef APP_HW_HW_TASK_HPP_
#define APP_HW_HW_TASK_HPP_

#include "hw/status_led.hpp"
#include "hw/strip_manager.hpp"
#include "oshal/event.hpp"
#include "oshal/task.hpp"

namespace app::hw {

/// @brief Owns the app_hw task, its wake event-flag group, the status LED,
///     and the strip manager.
///
/// HwTask is a process-wide singleton.  Producers (StripManager's mailbox,
/// CommandManager's UART RX) post bits to the contained event group; the
/// task loop calls WaitAny on it.
class HwTask {
 public:
  /// @brief Access the process-wide singleton.
  /// @return Reference to the HwTask singleton.
  static HwTask& Instance();

  HwTask(const HwTask&) = delete;
  HwTask& operator=(const HwTask&) = delete;

  /// @brief Create and start the underlying Zephyr task.
  /// @param name             Diagnostic task name.
  /// @param stack_size_bytes Stack size in bytes.
  /// @param priority         Task priority (lower = higher).
  /// @return STATUS_OK on success, or a negative status code.
  int Start(const char* name, std::size_t stack_size_bytes, int priority);

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
  /// @return Reference to the owned event group.
  oshal::EventFlagGroup& EventGroup() { return event_group_; }

  /// @brief Access the status LED owned by this HwTask.
  /// @return Reference to the status LED.
  StatusLed& GetStatusLed() { return status_led_; }

  /// @brief Access the strip manager owned by this HwTask.
  /// @return Reference to the strip manager.
  StripManager& GetStrip() { return strip_manager_; }

  HwTask() = default;

 private:
  /// @brief One-time setup — prints the startup banner.
  /// @return True when the HW executor loop may begin.
  bool Setup();

  /// @brief Main event loop — blocks on event_group_, dispatches to
  ///     CommandManager and StripManager, then blinks the status LED.
  /// @return True to keep running, false on fatal error.
  bool Loop();

  /// @brief C-callable trampoline for oshal::TaskConfig.
  static bool SetupTrampoline(void* context);

  /// @brief C-callable trampoline for oshal::TaskConfig.
  static bool LoopTrampoline(void* context);

  oshal::EventFlagGroup event_group_;
  StatusLed status_led_;
  StripManager strip_manager_{event_group_};
  oshal::TaskHandle task_;
};

}  // namespace app::hw

#endif /* APP_HW_HW_TASK_HPP_ */
