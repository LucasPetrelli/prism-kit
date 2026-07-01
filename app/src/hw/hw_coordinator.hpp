#ifndef APP_HW_HW_COORDINATOR_HPP_
#define APP_HW_HW_COORDINATOR_HPP_

namespace app::hw {

/// @brief Create and start the app_hw task.
///
/// Must be called after HwTask::Instance()'s Strip() and StatusLed() have
/// been configured via prism::Initialize().  The task runs the hardware
/// executor loop: draining strip frames, servicing the command-port
/// protocol, and blinking the status LED.
///
/// Idempotent — returns STATUS_OK immediately if the task is already
/// running, making repeated calls safe from multiple initialisation paths.
///
/// @return STATUS_OK on success, or a negative project-defined status code.
/// @pre HwTask::Instance().GetStrip().Configure() and
///     HwTask::Instance().GetStatusLed().Configure() have been called with
///     valid pointers.
int StartHwExecutor();

}  // namespace app::hw

#endif /* APP_HW_HW_COORDINATOR_HPP_ */
