#ifndef APP_APP_HPP_
#define APP_APP_HPP_

namespace app {

/// @brief Run the application after BAL has prepared board-owned resources.
/// @param context Optional task context supplied by the launcher.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     startup fails before the steady-state loop begins.
/// @pre BAL completed board bring-up for every board-owned resource required by
///     the application.
/// @note Steady-state application flows are allowed to run indefinitely and may
///     therefore never return on success. When Zephyr schedules APP as a task,
///     this function is the task main for that application task.
int run(void* context = nullptr);

}  // namespace app

#endif /* APP_APP_HPP_ */