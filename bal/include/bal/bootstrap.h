#ifndef BAL_BOOTSTRAP_H_
#define BAL_BOOTSTRAP_H_

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Validate HAL state, initialize BAL-owned objects, and call APP.
/// @return 0 on success, or a negative errno-style code if HAL validation or
///     board bring-up fails.
/// @note This function is the owned transition from the Zephyr-facing startup
///     path into the board and application layers.
int bal_bootstrap_run(void);

#ifdef __cplusplus
}
#endif

#endif /* BAL_BOOTSTRAP_H_ */