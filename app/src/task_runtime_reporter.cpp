#include <cstdio>

#include "app/task_runtime_reporter.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/status.h"

namespace {

constexpr std::size_t kCpuRuntimeTextCapacity = 16U;
constexpr std::size_t kStackUsageTextCapacity = 24U;

/*
 * Keep the emitted diagnostics on one stable line shape so callers do not have
 * to interpret a separate baseline format. Fields that are not yet available
 * are rendered as `n/a` until the required runtime history or backend support
 * exists.
 */
int print_task_runtime_info(const oshal::TaskRuntimeInfo &task_info)
{
	char cpu_runtime_text[kCpuRuntimeTextCapacity] = "n/a";
	char current_stack_text[kStackUsageTextCapacity] = "n/a";

	if (task_info.cpu_runtime_percent != oshal::kTaskRuntimePercentUnavailable) {
		static_cast<void>(std::snprintf(
			cpu_runtime_text, sizeof(cpu_runtime_text), "%u%%",
			static_cast<unsigned int>(task_info.cpu_runtime_percent)));
	}

	if (task_info.current_stack_used_bytes != oshal::kTaskStackUsageUnavailable) {
		static_cast<void>(std::snprintf(
			current_stack_text, sizeof(current_stack_text), "%zu",
			task_info.current_stack_used_bytes));
	}

	return oshal::debug_port.printf(
		"Task %s runtime: cpu=%s stack cfg=%zu tracked=%zu unused=%zu high=%zu current=%s\n",
		task_info.name, cpu_runtime_text,
		task_info.configured_stack_size_bytes, task_info.tracked_stack_size_bytes,
		task_info.unused_stack_bytes, task_info.high_water_stack_used_bytes,
		current_stack_text);
}
} // namespace

namespace app {

TaskRuntimeReporter::TaskRuntimeReporter(const oshal::TaskHandle &task_handle)
	: task_handle_(task_handle)
{
}

int TaskRuntimeReporter::report()
{
	oshal::TaskRuntimeInfo task_info = {};
	const int ret = task_handle_.runtime_info(&task_info);
	if (ret < 0) {
		return ret;
	}

	const int print_ret = print_task_runtime_info(task_info);
	if (print_ret < 0) {
		return print_ret;
	}

	return STATUS_OK;
}

} // namespace app