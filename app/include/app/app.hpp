#ifndef APP_APP_HPP_
#define APP_APP_HPP_

#include <cstdint>

#include "hw/controller_command.hpp"
#include "oshal/event.hpp"
#include "oshal/timed_event.hpp"
#include "prism/controller.hpp"

namespace app {

/// @brief Owns the steady-state APP task loop and its mutable state.
///
/// AppTask is a process-wide singleton.  The BAL bootstrap layer calls the
/// static C-callable trampolines; they forward to the private instance
/// methods which hold the real logic and state.
class AppTask {
 public:
  /// @brief Event bitmask posted when a controller command arrives from the
  ///     protocol layer (HW thread).
  static constexpr std::uint32_t kCommandEventMask = oshal::UserEvent(2);

  /// @brief Event bitmask posted by the run-schedule timer when a
  ///     controller-instruction delay has elapsed.
  static constexpr std::uint32_t kTimeoutEventMask = oshal::UserEvent(3);

  /// @brief Access the process-wide singleton.
  /// @return Reference to the AppTask singleton.
  static AppTask& Instance();

  AppTask(const AppTask&) = delete;
  AppTask& operator=(const AppTask&) = delete;

  /// @brief Access the animation controller owned by this task.
  /// @return Reference to the prism::Controller instance.
  prism::Controller& GetController();

  /// @brief C-callable setup trampoline for bal::RunBootstrap.
  /// @param context Unused — all state is accessed via Instance().
  /// @return True when the steady-state loop may begin.
  static bool SetupTrampoline(void* context);

  /// @brief C-callable loop trampoline for bal::RunBootstrap.
  /// @param context Unused — all state is accessed via Instance().
  /// @return True to keep the APP task running.
  static bool LoopTrampoline(void* context);

 private:
  AppTask() = default;

  /// @brief Bind the controller to the strip, apply the rainbow default,
  ///     and wire the command mailbox to the protocol layer.
  /// @return True when the steady-state loop may begin.
  bool Setup();

  /// @brief Wait for controller commands and dispatch them.
  /// @return True to keep running.
  bool Loop();

  /// @brief Drain all pending commands from the mailbox and dispatch them
  ///     to the controller.
  void UpdateInstructions();

  /// @brief Static trampoline for ScheduleCallback — starts the run
  ///     timer to wake Loop() after the given delay.
  /// @param delay_ms Milliseconds until the next Run() should occur.
  static void OnScheduleNextRun(std::uint32_t delay_ms);

  /// @brief High-level animation controller.
  prism::Controller controller_;
  /// @brief Cached LED count for range construction.
  std::uint8_t led_count_;
  /// @brief Event group for the APP-task loop — signalled by the protocol
  ///     layer (kCommandEventMask) and the run-schedule timer
  ///     (kTimeoutEventMask).
  oshal::EventFlagGroup app_event_group_;
  /// @brief One-shot timer that fires when a timed controller instruction
  ///     needs re-arming.  Posts kTimeoutEventMask to app_event_group_.
  oshal::TimedEvent run_timer_{app_event_group_, kTimeoutEventMask};
  /// @brief Mailbox carrying ControllerCommandMessage from HW→APP thread.
  app::hw::ControllerCommandMailbox command_mailbox_{app_event_group_,
                                                     kCommandEventMask};
};

}  // namespace app

#endif /* APP_APP_HPP_ */