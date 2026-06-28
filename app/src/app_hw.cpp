#include <cstddef>
#include <cstdint>

#include "bal/ws2812_strip.hpp"
#include "hw/command_manager.hpp"
#include "hw/hw_constants.hpp"
#include "hw/hw_coordinator.hpp"
#include "hw/hw_task.hpp"
#include "hw/status_led.hpp"
#include "hw/strip_manager.hpp"
#include "oshal/event.hpp"
#include "oshal/status.h"
#include "oshal/task.hpp"

namespace {

/*
 * Construction order is intentional:
 *   1. g_event_group   — the shared event-flag group, constructed first
 *   2. g_hw_task       — owns a reference to g_event_group
 *   3. g_strip_manager — mailbox posts to g_event_group on Send
 *
 * Producers (StripManager's mailbox, CommandManager's UART RX) post bits
 * to g_event_group.  The task loop in LoopCallback blocks on
 * g_hw_task.event_group().
 */

oshal::EventFlagGroup g_event_group;
app::hw::HwTask g_hw_task{g_event_group};
app::hw::StripManager g_strip_manager{g_event_group};

/// @brief C-callable setup trampoline for oshal::TaskConfig.
/// @param context Unused — all state is accessed via singletons.
/// @return True when the HW executor loop may begin.
bool SetupCallback(void* context) {
  static_cast<void>(context);

  /*
   * Managers are already configured by prism::initialize() before the
   * task starts.  Only the startup banner needs to be printed from
   * within the task context.
   */
  auto& cmd_mgr = app::hw::CommandManager::Instance();
  return cmd_mgr.PrintBanner(bal::ws2812_strip().name());
}

/// @brief C-callable loop trampoline for oshal::TaskConfig.
/// @param context Unused — all state is accessed via singletons.
/// @return True to keep running, false on fatal error.
bool LoopCallback(void* context) {
  static_cast<void>(context);

  auto& strip_mgr = app::hw::StripManager::Instance();
  auto& cmd_mgr = app::hw::CommandManager::Instance();
  auto& led = app::hw::StatusLed::Instance();

  /*
   * Block until a frame arrives from APP, UART data arrives on the
   * command port, or the idle tick fires (for blink timing).
   * Matching events are atomically cleared on successful return.
   */
  const std::uint32_t events = g_hw_task.event_group().WaitAny(
    strip_mgr.frame_event_mask() | cmd_mgr.rx_event_mask(),
    app::hw::kTaskIdleSleepMs);

  /* Run the protocol engine — feeds RX bytes through the parser and
   * retries any pending TX frame.  Only runs when UART data arrived. */
  if ((events & cmd_mgr.rx_event_mask()) != 0U) {
    cmd_mgr.Run();
  }

  /* Drain and apply any committed frames from APP.  The EventMailbox
   * re-posts the frame event mask if more frames arrive during
   * processing. */
  if ((events & strip_mgr.frame_event_mask()) != 0U) {
    if (!strip_mgr.TryApplyLatest()) {
      return false;
    }
  }

  return led.Blink();
}

}  // namespace

namespace app::hw {

StripManager& StripManager::Instance() { return g_strip_manager; }

int StartHwExecutor() {
  /* Idempotent guard — return STATUS_OK if the task is already running.
   * This makes repeated calls safe from multiple initialisation paths. */
  if (g_hw_task.IsRunning()) {
    return STATUS_OK;
  }

  return g_hw_task.Start("app_hw", SetupCallback, LoopCallback, nullptr,
                         kTaskStackSizeBytes, kTaskPriority);
}

}  // namespace app::hw