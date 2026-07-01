#include "hw_task.hpp"

#include "oshal/status.h"

namespace app::hw {

HwTask::HwTask(oshal::EventFlagGroup& event_group)
    : event_group_(event_group) {}

int HwTask::Start(const char* name, oshal::TaskSetup setup,
                  oshal::TaskLoop loop, void* context,
                  std::size_t stack_size_bytes, int priority) {
  if (task_.IsValid()) {
    return STATUS_OK;
  }

  oshal::TaskConfig config;
  config.name = name;
  config.setup = setup;
  config.loop = loop;
  config.context = context;
  config.stack_size_bytes = stack_size_bytes;
  config.priority = priority;
  return oshal::TaskHandle::Create(task_, config);
}

bool HwTask::IsRunning() const { return task_.IsValid(); }

bool HwTask::HasExited() const { return task_.HasExited(); }

int HwTask::ExitCode(int* out_code) const { return task_.ExitCode(out_code); }

}  // namespace app::hw
