#ifndef HAL_SYSTEM_H_
#define HAL_SYSTEM_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Report whether the HAL startup stage completed successfully.
/// @return True when HAL startup completed successfully, otherwise false.
/// @note The startup stage runs before main() through SYS_INIT().
bool hal_system_ready(void);

/// @brief Return the status code produced by the HAL startup stage.
/// @return 0 when HAL startup succeeded, or a negative errno-style code that
///     explains the failure.
int hal_system_status(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTEM_H_ */