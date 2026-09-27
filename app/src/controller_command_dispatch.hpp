#ifndef APP_CONTROLLER_COMMAND_DISPATCH_HPP_
#define APP_CONTROLLER_COMMAND_DISPATCH_HPP_

#include "hw/controller_command.hpp"
#include "prism/controller.hpp"

namespace app {

/// @brief Result of applying one APP-side controller command.
struct ControllerCommandResult {
  /// @brief Whether a program mutation was attempted.
  bool mutation_attempted{false};
  /// @brief Result of AddInstruction when mutation_attempted is true.
  prism::ControllerStatus mutation_status{prism::ControllerStatus::kSuccess};
  /// @brief Whether Start was attempted for a start command.
  bool start_attempted{false};
  /// @brief Result of Start when start_attempted is true.
  prism::ControllerStatus start_status{prism::ControllerStatus::kSuccess};
  /// @brief Whether the success-gated immediate Run tick was attempted.
  bool initial_run_attempted{false};
  /// @brief Result of the immediate Run tick when attempted.
  prism::ControllerStatus initial_run_status{prism::ControllerStatus::kSuccess};
};

/// @brief Apply one decoded protocol command to the controller.
/// @param controller APP-owned controller receiving the command.
/// @param message Decoded command copied from the HW-to-APP mailbox.
/// @return Mutation/start/progress statuses for APP diagnostics.
ControllerCommandResult DispatchControllerCommand(
  prism::Controller& controller,
  const app::hw::ControllerCommandMessage& message);

/// @brief Advance the controller for a timer-driven progress event.
/// @param controller APP-owned controller.
/// @return Status from the progress tick.
prism::ControllerStatus RunControllerProgress(prism::Controller& controller);

}  // namespace app

#endif  // APP_CONTROLLER_COMMAND_DISPATCH_HPP_
