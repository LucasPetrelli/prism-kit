#ifndef APP_PRISM_HW_BACKEND_INTERNAL_HPP_
#define APP_PRISM_HW_BACKEND_INTERNAL_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "bal/led.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/task.hpp"
#include "prism/strip.hpp"

namespace app::internal {

/// @brief Maximum number of logical pixels the current HW mailbox can stage.
/// @note The current HW backend is sized for the Seeed XIAO's 7-pixel strip.
///     This ceiling is intentionally kept small so the APP-owned double buffer
///     remains predictable in SRAM.
constexpr std::size_t kPrismHwMailboxFrameCapacity = 16U;

/// @brief One committed logical strip frame published from APP to app_hw.
/// @note APP stages colors in Prism Kit types first, then app_hw translates the
///     committed frame into BAL strip mutations.
struct SharedFrame {
  /// @brief Number of valid pixels in @ref colors.
  std::size_t led_count = 0U;
  /// @brief Staged logical RGB colors for the committed frame.
  std::array<prism::RgbColor, kPrismHwMailboxFrameCapacity> colors = {};
};

/// @brief Double-buffered mailbox shared between app::run() and app_hw.
/// @note The producer always writes the inactive frame slot before publishing
///     the frame index and generation counters. The consumer polls the
///     generation counter and then reads the published slot.
struct SharedMailbox {
  /// @brief Double-buffered committed frames.
  SharedFrame frames[2] = {};
  /// @brief Monotonic publish counter observed by app_hw.
  std::atomic<std::uint32_t> published_generation = 0U;
  /// @brief Index of the most recently published frame inside @ref frames.
  std::atomic<std::uint8_t> published_frame_index = 0U;
};

/// @brief APP-owned execution helpers captured for the HW backend task.
struct RuntimeServices {
  /// @brief Board-owned status LED controlled by the HW executor.
  bal::Led* status_led = nullptr;
  /// @brief OSHAL debug port used by the HW executor for diagnostics.
  oshal::DebugPort* debug_port = nullptr;
};

/// @brief Shared Prism Kit HW mailbox instance owned by APP.
extern SharedMailbox g_prism_hw_mailbox;
/// @brief Handle for the APP-owned hardware executor task.
extern oshal::TaskHandle g_prism_hw_task;
/// @brief Captured APP-owned services forwarded to the HW executor.
extern RuntimeServices g_prism_runtime_services;

/// @brief Start the APP-owned HW executor task if it is not running yet.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int ensure_prism_hw_started();

/// @brief Publish one committed logical frame to the HW executor task.
/// @param frame Caller-owned committed frame to copy into the shared mailbox.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int publish_prism_hw_frame(const SharedFrame& frame);

/// @brief Task entry that applies committed Prism Kit frames to BAL.
/// @param context Optional task context. The current HW backend does not use it.
/// @return STATUS_OK on success, or a negative project-defined status code when
///     the HW executor cannot continue.
int run_prism_hw_task(void* context);

}  // namespace app::internal

#endif /* APP_PRISM_HW_BACKEND_INTERNAL_HPP_ */