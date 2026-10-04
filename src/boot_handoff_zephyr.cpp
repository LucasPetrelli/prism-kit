#include <zephyr/kernel/thread_stack.h>

#include <cstddef>

#include "app/app.hpp"
#include "bal/bootstrap.hpp"
#include "hw/hw_constants.hpp"

namespace {

constexpr std::size_t kAppTaskStackSizeBytes = 1024U;
K_THREAD_STACK_DEFINE(g_app_task_stack, kAppTaskStackSizeBytes);
K_THREAD_STACK_DEFINE(g_hw_task_stack, app::hw::kTaskStackSizeBytes);
app::AppTaskStartupContext g_app_task_startup_context{
  g_hw_task_stack, K_THREAD_STACK_SIZEOF(g_hw_task_stack)};

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int oshal_main_handoff(void) {
  return bal::RunBootstrap(app::AppTask::SetupTrampoline,
                           app::AppTask::LoopTrampoline, g_app_task_stack,
                           K_THREAD_STACK_SIZEOF(g_app_task_stack),
                           &g_app_task_startup_context);
}