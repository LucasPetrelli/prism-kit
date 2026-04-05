#ifndef BAL_BOOTSTRAP_H_
#define BAL_BOOTSTRAP_H_

#ifdef __cplusplus
extern "C" {
#endif

/// @brief C ABI for the application entry point BAL receives after board bring-up.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     startup fails before the steady-state loop begins.
typedef int (*bal_application_entry_t)(void);

/// @brief Validate OSHAL state, initialize BAL-owned objects, and call the supplied APP entry.
/// @param app_entry C ABI application entry point provided by the Zephyr-facing
///     startup path.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     OSHAL validation, board bring-up, or application hand-off fails.
/// @note This function is the owned transition from the Zephyr-facing startup
///     path into the board and application layers.
int bal_run(bal_application_entry_t app_entry);

#ifdef __cplusplus
}
#endif

#endif /* BAL_BOOTSTRAP_H_ */