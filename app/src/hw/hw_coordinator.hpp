#ifndef APP_HW_HW_COORDINATOR_HPP_
#define APP_HW_HW_COORDINATOR_HPP_

namespace app::hw {

/// @brief Create and start the app_hw task.
///
/// Must be called after StripManager, CommandManager, and StatusLed have
/// been configured.  The task runs the hardware executor loop: draining
/// strip frames, servicing the command-port protocol, and blinking the
/// status LED.
///
/// Idempotent — returns STATUS_OK immediately if the task is already
/// running, making repeated calls safe from multiple initialisation paths.
///
/// @return STATUS_OK on success, or a negative project-defined status code.
/// @pre StripManager::Instance(), CommandManager::Instance(), and
///     StatusLed::Instance() have been configured with valid pointers.
int StartHwExecutor();

}  // namespace app::hw

#endif /* APP_HW_HW_COORDINATOR_HPP_ */
