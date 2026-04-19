#ifndef OSHAL_SYSTEM_H_
#define OSHAL_SYSTEM_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Report whether the OSHAL startup stage completed successfully.
/// @return True when OSHAL startup completed successfully, otherwise false.
/// @note The startup validation stage runs before main() through SYS_INIT().
bool oshal_system_ready(void);

/// @brief Return the status code produced by the OSHAL startup stage.
/// @return STATUS_OK when OSHAL startup succeeded, or a negative
/// project-defined
///     status code that explains the failure.
int oshal_system_status(void);

/// @brief Continue startup beyond the OSHAL-owned Zephyr entry point.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     board bring-up or application hand-off fails.
/// @note This hook is declared by OSHAL and implemented by the outer firmware
///     composition layer so OSHAL does not depend directly on BAL or APP.
int oshal_main_handoff(void);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_SYSTEM_H_ */