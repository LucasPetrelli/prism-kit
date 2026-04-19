#ifndef BAL_BOOTSTRAP_HPP_
#define BAL_BOOTSTRAP_HPP_

namespace bal {

/// @brief C++ task-entry signature BAL receives after board bring-up.
/// @param context Optional caller-owned context passed into the APP task entry.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     startup fails before the steady-state loop begins.
using ApplicationEntry = int (*)(void* context);

/// @brief Validate OSHAL state, initialize BAL-owned objects, and launch the
/// supplied APP task.
/// @param app_entry Application entry point provided by the caller.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     OSHAL validation, board bring-up, or application task launch fails.
/// @note This function is the owned transition from the Zephyr-facing startup
///     path into the board and application layers.
int run_bootstrap(ApplicationEntry app_entry);

}  // namespace bal

#endif /* BAL_BOOTSTRAP_HPP_ */