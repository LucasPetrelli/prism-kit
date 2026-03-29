#ifndef BAL_BOOTSTRAP_HPP_
#define BAL_BOOTSTRAP_HPP_

#include "bal/bootstrap.h"

namespace bal {

/// @brief Run BAL bootstrap through the stable C ABI entry point.
/// @return 0 on success, or a negative errno-style code if HAL validation or
///     board bring-up fails.
inline int run_bootstrap()
{
	return bal_bootstrap_run();
}

} // namespace bal

#endif /* BAL_BOOTSTRAP_HPP_ */