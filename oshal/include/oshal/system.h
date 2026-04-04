#ifndef OSHAL_SYSTEM_H_
#define OSHAL_SYSTEM_H_

#include <stdbool.h>

#include "status/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Report whether the OSHAL startup stage completed successfully.
/// @return True when OSHAL startup completed successfully, otherwise false.
/// @note The startup stage runs before main() through SYS_INIT().
bool oshal_system_ready(void);

/// @brief Return the status code produced by the OSHAL startup stage.
/// @return STATUS_OK when OSHAL startup succeeded, or a negative project-defined
///     status code that explains the failure.
int oshal_system_status(void);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_SYSTEM_H_ */