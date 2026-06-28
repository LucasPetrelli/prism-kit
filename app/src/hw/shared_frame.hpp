#ifndef APP_HW_SHARED_FRAME_HPP_
#define APP_HW_SHARED_FRAME_HPP_

#include <array>
#include <cstddef>

#include "prism/color.hpp"

namespace app::hw {

/// @brief Maximum number of logical pixels the current HW mailbox can stage.
/// @note The current HW backend is sized for the Seeed XIAO's 7-pixel strip.
///     This ceiling is intentionally kept small so the APP-owned double buffer
///     remains predictable in SRAM.
constexpr std::size_t kSharedFrameCapacity = 16U;

/// @brief One committed logical strip frame published from APP to app_hw.
/// @note APP stages colors in Prism Kit types first, then app_hw translates the
///     committed frame into BAL strip mutations.
struct SharedFrame {
  /// @brief Number of valid pixels in @ref colors.
  std::size_t led_count = 0U;
  /// @brief Staged logical RGB colors for the committed frame.
  std::array<prism::RgbColor, kSharedFrameCapacity> colors = {};
};

}  // namespace app::hw

#endif /* APP_HW_SHARED_FRAME_HPP_ */
