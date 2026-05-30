#ifndef APP_PRISM_HW_MAILBOX_HPP_
#define APP_PRISM_HW_MAILBOX_HPP_

#include <array>
#include <cstddef>

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

}  // namespace app::internal

#endif /* APP_PRISM_HW_MAILBOX_HPP_ */
