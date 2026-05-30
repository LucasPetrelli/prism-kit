#ifndef APP_PRISM_HW_MAILBOX_HPP_
#define APP_PRISM_HW_MAILBOX_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

/// @brief Lock-free SPSC double-buffered mailbox for frame delivery.
///
/// The producer writes to the inactive frame slot and then publishes a new
/// generation counter with release semantics.  The consumer polls the
/// generation counter with acquire semantics and reads the published slot.
class PrismHwMailbox {
 public:
  /// @brief Maximum frame capacity exposed for dependent types.
  static constexpr std::size_t kFrameCapacity = kPrismHwMailboxFrameCapacity;

  /// @brief Publish a committed frame to the mailbox (producer side).
  /// @param frame Caller-owned frame to copy into the inactive slot.
  /// @return STATUS_OK on success, or a negative project-defined status code.
  /// @note Uses release semantics on the generation counter so the consumer
  ///     observes the complete frame write.
  int Publish(const SharedFrame& frame);

  /// @brief Poll for a new frame (consumer side).
  /// @param[out] out_frame Receives the committed frame when a newer
  ///     generation is available.
  /// @param last_seen_generation The generation the caller last consumed.
  /// @return true when a newer generation was found and out_frame is valid.
  /// @note Uses acquire semantics on the generation counter so the frame read
  ///     observes the producer's write.
  bool Poll(SharedFrame& out_frame, std::uint32_t last_seen_generation) const;

  /// @brief Snapshot the current published generation for setup
  ///     synchronisation.
  /// @return The current generation counter (acquire).
  std::uint32_t SnapshotGeneration() const;

 private:
  /// @brief Double-buffered committed frames.
  SharedFrame frames_[2] = {};
  /// @brief Monotonic publish counter observed by the consumer.
  std::atomic<std::uint32_t> published_generation_ = 0U;
  /// @brief Index of the most recently published frame inside @ref frames_.
  std::atomic<std::uint8_t> published_frame_index_ = 0U;
};

/// @brief Accessor for the process-wide PrismHwMailbox singleton.
/// @return Reference to the singleton mailbox instance.
PrismHwMailbox& PrismHwMailboxInstance();

}  // namespace app::internal

#endif /* APP_PRISM_HW_MAILBOX_HPP_ */
