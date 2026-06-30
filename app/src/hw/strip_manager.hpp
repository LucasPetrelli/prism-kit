#ifndef APP_HW_STRIP_MANAGER_HPP_
#define APP_HW_STRIP_MANAGER_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

#include "bal/ws2812_strip.hpp"
#include "hw/shared_frame.hpp"
#include "oshal/event.hpp"
#include "oshal/event_mailbox.hpp"
#include "prism/color.hpp"
#include "prism/strip.hpp"

namespace app::hw {

// Forward-declare for the StripLedView → StripManager dependency.
class StripManager;

/// @brief Concrete prism::StripLed view that delegates to StripManager.
///
/// Each view stores only its own zero-based index.  All colour staging
/// and readiness queries are forwarded to the StripManager singleton.
class StripLedView : public prism::StripLed {
 public:
  StripLedView() = default;

  /// @brief Assign the zero-based pixel index inside the owning strip.
  /// @param index Zero-based strip index.
  void SetIndex(std::size_t index) { index_ = index; }

  bool IsReady() const override;
  int SetColor(const prism::RgbColor& color) override;
  prism::RgbColor Color() const override;
  std::size_t Index() const override { return index_; }

 private:
  std::size_t index_ = 0U;
};

/// @brief Owns the WS2812 strip backend, the APP-visible staged frame,
///     and the mailbox that delivers committed frames to the HW task.
///
/// StripManager implements prism::Strip so that APP code calls the
/// standard fill()/show() contract.  show() posts the staged frame into
/// the internal mailbox; the HW task loop drains frames via
/// TryApplyLatest().
class StripManager : public prism::Strip {
 public:
  /// @brief Event bitmask posted when a frame enters the mailbox.
  static constexpr std::uint32_t kFrameEventMask = oshal::UserEvent(0);

  /// @brief Access the process-wide singleton.
  /// @return Reference to the StripManager singleton.
  static StripManager& Instance();

  StripManager(const StripManager&) = delete;
  StripManager& operator=(const StripManager&) = delete;

  /// @brief Wire the backend strip and initialise staged state.
  /// @param backend_strip BAL-owned strip (must be ready).
  /// @param led_count     Number of logical pixels (≤ kSharedFrameCapacity).
  /// @param strip_name    Human-readable name for the strip.
  /// @pre backend_strip has been validated by the caller.
  void Configure(bal::Ws2812Strip* backend_strip, std::size_t led_count,
                 const char* strip_name);

  // ---- prism::Strip interface ----------------------------------------

  const char* Name() const override;
  bool IsReady() const override;
  std::size_t LedCount() const override;

  prism::StripLed* Led(std::size_t index) override;
  const prism::StripLed* Led(std::size_t index) const override;

  int Fill(const prism::RgbColor& color) override;

  /// @brief Commit the staged frame into the mailbox for the HW task.
  /// @return STATUS_OK on success, or a negative status code.
  int Show() override;

  // ---- HW task side --------------------------------------------------

  /// @brief Drain all pending frames from the mailbox and apply them to
  ///     the strip.
  ///
  /// The mailbox auto-clears the frame event when the last message is
  /// consumed, so no explicit event acknowledge is needed here.
  /// @return false only on fatal hardware error.
  bool TryApplyLatest();

  /// @brief Access the event-flag group this mailbox posts to.
  /// @return Reference to the shared event group.
  oshal::EventFlagGroup& EventGroup() { return event_group_; }

  /// @brief Event mask the coordinator should wait on for frame arrival.
  /// @return kFrameEventMask.
  std::uint32_t FrameEventMask() const { return kFrameEventMask; }

  /// @brief Construct with a reference to the shared task-wake event group.
  /// @param event_group EventFlagGroup that the mailbox posts to on Send.
  /// @pre event_group must outlive this StripManager.
  explicit StripManager(oshal::EventFlagGroup& event_group);

  // ---- Internal helpers used by StripLedView -------------------------

  /// @brief Stage a colour for a single pixel.
  /// @param index Zero-based pixel index.
  /// @param color Requested logical RGB colour.
  /// @return STATUS_OK on success, or a negative status code.
  int SetLedColor(std::size_t index, const prism::RgbColor& color);

  /// @brief Return the currently staged colour for a single pixel.
  /// @param index Zero-based pixel index.
  /// @return Staged RGB colour (default-constructed if out of range).
  prism::RgbColor LedColor(std::size_t index) const;

 private:
  /// @brief Translate a committed frame into BAL strip mutations and flush.
  /// @param frame Frame to apply.
  /// @return STATUS_OK on success, or a negative status code.
  int ApplyFrame(const SharedFrame& frame);

  oshal::EventFlagGroup& event_group_;
  oshal::EventMailbox<sizeof(SharedFrame), 1U> mailbox_{event_group_,
                                                        kFrameEventMask};
  bal::Ws2812Strip* backend_strip_ = nullptr;

  // ---- prism::Strip backing state (was HardwareStrip) -----------------
  bool ready_ = false;
  std::size_t led_count_ = 0U;
  const char* name_ = "prism_hw_strip";
  SharedFrame staged_frame_ = {};
  std::array<StripLedView, kSharedFrameCapacity> led_views_ = {};
};

}  // namespace app::hw

#endif /* APP_HW_STRIP_MANAGER_HPP_ */
