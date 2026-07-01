
#include "hw/hw_constants.hpp"
#include "hw/hw_coordinator.hpp"
#include "hw/hw_task.hpp"
#include "oshal/status.h"

namespace app::hw {

int StartHwExecutor() {
  auto& hw = HwTask::Instance();

  /* Idempotent guard — return STATUS_OK if the task is already running.
   * This makes repeated calls safe from multiple initialisation paths. */
  if (hw.IsRunning()) {
    return STATUS_OK;
  }

  return hw.Start("app_hw", kTaskStackSizeBytes, kTaskPriority);
}

}  // namespace app::hw