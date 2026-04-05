#ifndef BAL_BOOTSTRAP_HPP_
#define BAL_BOOTSTRAP_HPP_

#include "bal/bootstrap.h"

namespace bal {

/// @brief Run BAL bootstrap through the stable C ABI entry point.
/// @param app_entry C ABI application entry point provided by the caller.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     OSHAL validation, board bring-up, or application hand-off fails.
inline int run_bootstrap(bal_application_entry_t app_entry)
{
	return bal_run(app_entry);
}

} // namespace bal

#endif /* BAL_BOOTSTRAP_HPP_ */