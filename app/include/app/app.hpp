#ifndef APP_APP_HPP_
#define APP_APP_HPP_

#include "app/app.h"

namespace app {

/// @brief Run the application through the stable C ABI entry point.
/// @return 0 on success, or a negative errno-style code if startup fails
///     before the steady-state loop begins.
inline int run()
{
	return app_run();
}

} // namespace app

#endif /* APP_APP_HPP_ */