#ifndef BAL_BOOTSTRAP_H_
#define BAL_BOOTSTRAP_H_

#include "status/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Validate OSHAL state, initialize BAL-owned objects, and call APP.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     OSHAL validation or board bring-up fails.
/// @note This function is the owned transition from the Zephyr-facing startup
///     path into the board and application layers.
int bal_bootstrap_run(void);

#ifdef __cplusplus
}
#endif

#endif /* BAL_BOOTSTRAP_H_ */