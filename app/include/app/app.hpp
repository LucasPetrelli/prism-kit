#ifndef APP_APP_HPP_
#define APP_APP_HPP_

#include <array>
#include <cstdint>

#include "prism/controller.hpp"

namespace app {

/// @brief Owns the steady-state APP task loop and its mutable state.
///
/// AppTask is a process-wide singleton.  The BAL bootstrap layer calls the
/// static C-callable trampolines; they forward to the private instance
/// methods which hold the real logic and state.
class AppTask {
 public:
  /// @brief Access the process-wide singleton.
  /// @return Reference to the AppTask singleton.
  static AppTask& Instance();

  AppTask(const AppTask&) = delete;
  AppTask& operator=(const AppTask&) = delete;

  /// @brief C-callable setup trampoline for bal::RunBootstrap.
  /// @param context Unused — all state is accessed via Instance().
  /// @return True when the steady-state loop may begin.
  static bool SetupTrampoline(void* context);

  /// @brief C-callable loop trampoline for bal::RunBootstrap.
  /// @param context Unused — all state is accessed via Instance().
  /// @return True to keep the APP task running.
  static bool LoopTrampoline(void* context);

 private:
  AppTask() = default;

  /// @brief Bind the controller to the strip and pre-build color instructions.
  /// @return True when the steady-state loop may begin.
  bool Setup();

  /// @brief Run one iteration of the steady-state application loop.
  /// @return True to keep running.
  bool Loop();

  /// @brief High-level animation controller.
  prism::Controller controller_;
  /// @brief Pre-built color-fill instructions, one per cycle color.
  std::array<prism::SetMultipleColor, 3U> instructions_;
  /// @brief Cached LED count for range construction.
  std::uint8_t led_count_;
  /// @brief Monotonically-increasing color-step index.
  std::uint32_t color_step_count_ = 0U;
};

}  // namespace app

#endif /* APP_APP_HPP_ */