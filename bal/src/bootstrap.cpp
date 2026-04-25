#include "bal/bootstrap.hpp"

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "oshal/status.h"
#include "oshal/system.h"
#include "oshal/task.hpp"

namespace {

/*
 * The XIAO SAMD21 only provides 32 KiB of SRAM for all Zephyr runtime state,
 * task stacks, and application data, so the APP thread stack must stay modest.
 * 1536 bytes is a deliberate compromise for this task's current needs: it gives
 * the application entry path and normal steady-state call depth comfortable
 * headroom without spending an unnecessarily large share of the board's limited
 * RAM budget.
 */
constexpr std::size_t kAppTaskStackSizeBytes = 1536U;
constexpr int kAppTaskPriority = 0;
oshal::TaskHandle g_app_task;

oshal::TaskConfig make_app_task_config(bal::ApplicationEntry app_entry) {
  oshal::TaskConfig config;

  config.name = "app_main";
  config.entry = app_entry;
  config.context = nullptr;
  config.stack_size_bytes = kAppTaskStackSizeBytes;
  config.priority = kAppTaskPriority;
  return config;
}

}  // namespace

int bal::run_bootstrap(ApplicationEntry app_entry) {
  if (app_entry == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  /* Refuse to continue if the earlier OSHAL-owned startup stage already failed.
   */
  if (!oshal_system_ready()) {
    return oshal_system_status();
  }

  /* Bring BAL-owned objects online before APP takes control. */
  int ret = bal::initialize_leds();
  if (ret < 0) {
    return ret;
  }

  ret = bal::initialize_ws2812_strips();
  if (ret < 0) {
    return ret;
  }

  /*
   * BAL still owns the transition into APP, but now hands execution to Zephyr
   * through the OSHAL task abstraction once board-owned resources are ready.
   */
  return oshal::TaskHandle::create(g_app_task, make_app_task_config(app_entry));
}