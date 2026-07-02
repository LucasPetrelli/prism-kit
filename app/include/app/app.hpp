#ifndef APP_APP_HPP_
#define APP_APP_HPP_

#include <cstdint>

#include "hw/controller_command.hpp"
#include "oshal/event.hpp"
#include "oshal/event_mailbox.hpp"
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

  /// @brief Access the process-wide singleton.
  /// @return Reference to the AppTask singleton.
  static AppTask& Instance();

  AppTask(const AppTask&) = delete;
  AppTask& operator=(const AppTask&) = delete;

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

  /// @brief High-level animation controller.
  prism::Controller controller_;
  /// @brief Cached LED count for range construction.
  std::uint8_t led_count_;
  /// @brief Event group that the protocol post to when a command arrives.
  oshal::EventFlagGroup command_event_group_;
  /// @brief Mailbox carrying ControllerCommandMessage from HW→APP thread.
  oshal::EventMailbox<sizeof(app::hw::ControllerCommandMessage), 4U>
    command_mailbox_{command_event_group_, kCommandEventMask};
};

}  // namespace app

#endif /* APP_APP_HPP_ */