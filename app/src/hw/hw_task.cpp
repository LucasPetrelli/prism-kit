#include "hw_task.hpp"

#include "bal/ws2812_strip.hpp"
#include "hw/command_manager.hpp"
#include "hw/hw_constants.hpp"
#include "oshal/status.h"

namespace {

app::hw::HwTask g_hw_task_instance;

}  // namespace

namespace app::hw {

HwTask& HwTask::Instance() { return g_hw_task_instance; }

int HwTask::Start(const char* name, std::size_t stack_size_bytes,
                  int priority) {
  if (task_.IsValid()) {
    return STATUS_OK;
  }

  oshal::TaskConfig config;
  config.name = name;
  config.setup = SetupTrampoline;
  config.loop = LoopTrampoline;
  config.context = this;
  config.stack_size_bytes = stack_size_bytes;
  config.priority = priority;
  return oshal::TaskHandle::Create(task_, config);
}

bool HwTask::IsRunning() const { return task_.IsValid(); }

bool HwTask::HasExited() const { return task_.HasExited(); }

int HwTask::ExitCode(int* out_code) const { return task_.ExitCode(out_code); }

bool HwTask::SetupTrampoline(void* context) {
  auto* self = static_cast<HwTask*>(context);
  return (self != nullptr) ? self->Setup() : false;
}

bool HwTask::LoopTrampoline(void* context) {
  auto* self = static_cast<HwTask*>(context);
  return (self != nullptr) ? self->Loop() : false;
}

bool HwTask::Setup() {
  auto& cmd_mgr = CommandManager::Instance();
  return cmd_mgr.PrintBanner(bal::GetWs2812Strip().Name());
}

bool HwTask::Loop() {
  auto& cmd_mgr = CommandManager::Instance();

  const std::uint32_t events = event_group_.WaitAny(
    strip_manager_.FrameEventMask() | cmd_mgr.RxEventMask(), kTaskIdleSleepMs);

  /* Run the protocol engine — feeds RX bytes through the parser and
   * retries any pending TX frame.  Only runs when UART data arrived. */
  if ((events & cmd_mgr.RxEventMask()) != 0U) {
    cmd_mgr.Run();
  }

  /* Drain and apply any committed frames from APP.  The EventMailbox
   * re-posts the frame event mask if more frames arrive during
   * processing. */
  if ((events & strip_manager_.FrameEventMask()) != 0U) {
    if (!strip_manager_.TryApplyLatest()) {
      return false;
    }
  }

  return status_led_.Blink();
}

}  // namespace app::hw
