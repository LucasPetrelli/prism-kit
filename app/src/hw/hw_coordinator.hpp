#ifndef APP_HW_HW_COORDINATOR_HPP_
#define APP_HW_HW_COORDINATOR_HPP_

#include <cstddef>

namespace app::hw {

/// @brief Create and start the app_hw task.
/// @param stack_buffer Caller-owned stack object for the app_hw task.
/// @param stack_size_bytes Usable size of @p stack_buffer in bytes.
///
/// HwTask::Setup() configures the HW managers (strip, command port, status
/// LED) when the task starts.  The task then runs the hardware executor
/// loop: draining strip frames, servicing the command-port protocol, and
/// blinking the status LED.
///
/// Idempotent — returns STATUS_OK immediately if the task is already
/// running, making repeated calls safe from multiple initialisation paths.
///
/// @return STATUS_OK on success, or a negative project-defined status code.
int StartHwExecutor(void* stack_buffer, std::size_t stack_size_bytes);

}  // namespace app::hw

#endif /* APP_HW_HW_COORDINATOR_HPP_ */
