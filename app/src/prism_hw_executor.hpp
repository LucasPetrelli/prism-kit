#ifndef APP_PRISM_HW_EXECUTOR_HPP_
#define APP_PRISM_HW_EXECUTOR_HPP_

#include "bal/led.hpp"
#include "bal/ws2812_strip.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/event.hpp"
#include "oshal/event_mailbox.hpp"
#include "oshal/serial_port.hpp"
#include "oshal/task.hpp"
#include "prism_hw_mailbox.hpp"

namespace app::internal {

/// @brief Owns the app_hw task lifecycle and all HW executor behaviour.
///
/// The executor is default-constructed before `prism::initialize()` runs.
/// `Configure()` injects the runtime service pointers (validated by the
/// caller), and `Start()` creates the Zephyr-backed `app_hw` task.
///
/// Once running, the task consumes committed frames from the OSHAL mailbox,
/// applies them to the BAL-owned WS2812 strip, and blinks the board status
/// LED at a steady 0.5 Hz.
class PrismHwExecutor {
 public:
  PrismHwExecutor() = default;

  /// @brief Inject runtime service pointers before Start().
  /// @param status_led Board-owned status LED (must be ready).
  /// @param debug_port OSHAL debug port (must be ready).
  /// @param command_port Optional OSHAL command port (may be null).
  /// @pre All non-null pointers have been validated by the caller.
  void Configure(bal::Led* status_led, oshal::DebugPort* debug_port,
                 oshal::SerialPort* command_port);

  /// @brief Create and start the app_hw task if it is not running yet.
  /// @return STATUS_OK on success, or a negative project-defined status code.
  /// @pre Configure() has been called with valid pointers.
  int Start();

  /// @brief Publish a committed frame with task-lifecycle guards.
  /// @param frame Caller-owned frame to publish via the mailbox.
  /// @return STATUS_OK on success, or a negative status code (e.g.
  ///     STATUS_ERR_NOT_READY if the executor is not running).
  int PublishFrame(const SharedFrame& frame);

  /// @brief Query whether the app_hw task is currently valid.
  /// @return true when the task handle represents a live task.
  bool IsRunning() const;

 private:
  static constexpr std::uint32_t kIdleSleepMs = 10U;
  static constexpr std::size_t kTaskStackSizeBytes = 1024U;
  static constexpr int kTaskPriority = 0;

  /// @brief Desired status-LED half-period in milliseconds.
  /// @note 1000 ms half-period → toggle every 1 s → 0.5 Hz blink
  ///     (one complete on/off cycle every 2 s).
  static constexpr std::uint32_t kBlinkHalfPeriodMs = 1000U;

  /// @brief Loop ticks between status-LED toggles, derived from the
  ///     half-period.
  /// @note This couples to @ref kIdleSleepMs so the blink rate stays
  ///     correct when the sleep interval changes.
  static constexpr std::uint32_t kBlinkHalfPeriodTicks =
    kBlinkHalfPeriodMs / kIdleSleepMs;

  static_assert((kBlinkHalfPeriodMs % kIdleSleepMs) == 0U,
                "Blink half-period must be an exact multiple of the idle "
                "sleep interval.");

  /// @brief C-callable setup trampoline for oshal::TaskConfig.
  /// @param context Pointer to the PrismHwExecutor instance.
  /// @return True when the HW executor loop may begin.
  static bool SetupCallback(void* context);

  /// @brief C-callable loop trampoline for oshal::TaskConfig.
  /// @param context Pointer to the PrismHwExecutor instance.
  /// @return True to keep running, false on fatal error.
  static bool LoopCallback(void* context);

  /// @brief One-shot task setup: capture BAL singletons and print banners.
  /// @return True on success.
  bool Setup();

  /// @brief Steady-state task loop iteration.
  /// @return True to keep running, false on fatal error.
  bool Loop();

  /// @brief Drain all pending frames from the mailbox and apply them to
  ///     the strip.
  ///
  /// The mailbox auto-clears the frame event when the last message is
  /// consumed, so no explicit event acknowledge is needed here.
  /// @return false only on fatal hardware error.
  bool TryApplyLatest();

  /// @brief Translate a committed frame into BAL strip mutations and flush.
  /// @param frame Frame to apply.
  /// @return STATUS_OK on success, or a negative status code.
  int ApplyFrame(const SharedFrame& frame);

  /// @brief Print startup banners on the debug port and optional command
  ///     port.
  /// @return false when a port write fails.
  bool PrintStartupBanners();

  /// @brief Toggle the status LED at kBlinkHalfPeriodMs period.
  /// @return false only on fatal hardware error from the LED driver.
  bool BlinkStatusLed();

  /// @brief Event bitmask posted when a frame enters the mailbox.
  static constexpr std::uint32_t kFrameEventMask = oshal::UserEvent(0);

  /// @brief OSHAL-backed mailbox for frame delivery from APP to app_hw.
  /// @note Posts kFrameEventMask to @ref frame_event_ on successful Send.
  oshal::Event frame_event_;
  oshal::EventMailbox<sizeof(SharedFrame), 1U> mailbox_{frame_event_,
                                                        kFrameEventMask};

  bal::Ws2812Strip* backend_strip_ = nullptr;
  bal::Led* status_led_ = nullptr;
  oshal::DebugPort* debug_port_ = nullptr;
  oshal::SerialPort* command_port_ = nullptr;
  std::uint32_t blink_tick_ = 0U;
  oshal::TaskHandle task_;
};

/// @brief Accessor for the process-wide PrismHwExecutor singleton.
/// @return Reference to the singleton executor instance.
PrismHwExecutor& PrismHwExecutorInstance();

}  // namespace app::internal

#endif /* APP_PRISM_HW_EXECUTOR_HPP_ */
