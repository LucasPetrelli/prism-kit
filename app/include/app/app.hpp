#ifndef APP_APP_HPP_
#define APP_APP_HPP_

#include <cstdint>

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

  /// @brief One-time application setup after BAL prepared board resources.
  /// @return True when the steady-state loop may begin.
  bool Setup();

  /// @brief Run one iteration of the steady-state application loop.
  /// @return True to keep running.
  bool Loop();

  std::uint32_t color_step_count_ = 0U;
};

}  // namespace app

#endif /* APP_APP_HPP_ */