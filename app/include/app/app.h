#ifndef APP_APP_H_
#define APP_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Run the application after BAL has prepared board-owned resources.
/// @return 0 on success, or a negative errno-style code if startup fails
///     before the steady-state loop begins.
/// @pre BAL completed board bring-up for every board-owned resource required by
///     the application.
/// @note Steady-state application flows are allowed to run indefinitely and may
///     therefore never return on success.
int app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_H_ */