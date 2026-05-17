#ifndef APP_APP_HPP_
#define APP_APP_HPP_

namespace app {

/// @brief Run one-time application setup after BAL prepared board resources.
/// @param context Optional task context supplied by the launcher.
/// @return True when the steady-state application loop may begin, otherwise
///     false.
/// @pre BAL completed board bring-up for every board-owned resource required by
///     the application.
bool setup(void* context = nullptr);

/// @brief Run one iteration of the steady-state application loop.
/// @param context Optional task context supplied by the launcher.
/// @return True to keep the APP task running, otherwise false.
/// @pre APP setup() completed successfully for the current task instance.
bool loop(void* context = nullptr);

}  // namespace app

#endif /* APP_APP_HPP_ */