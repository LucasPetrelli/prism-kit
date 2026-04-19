#ifndef OSHAL_SAMD21_DMAC_CHANNEL_HPP_
#define OSHAL_SAMD21_DMAC_CHANNEL_HPP_

#include <soc.h>

#include <cstddef>
#include <cstdint>

namespace oshal::internal {

/// @brief Lightweight owner for one SAMD21 DMAC channel.
class Samd21DmacChannel final {
 public:
  /// @brief Completion callback invoked from DMAC IRQ context.
  /// @param context Opaque pointer supplied during configuration.
  /// @param transfer_error True when the DMAC channel reported an error.
  using CompletionCallback = void (*)(void* context, bool transfer_error);

  /// @brief Construct a DMAC channel owner for one hardware channel index.
  /// @param channel_index Zero-based SAMD21 DMAC channel index.
  explicit Samd21DmacChannel(std::uint8_t channel_index);

  /// @brief Configure this channel for triggered 32-bit register streaming.
  /// @param destination_register Register destination written by each beat.
  /// @param source_words Source buffer consumed by DMAC.
  /// @param word_count Number of 32-bit words to transfer per descriptor pass.
  /// @param trigger_source Peripheral trigger ID routed into this channel.
  /// @param loop_forever True to self-chain the descriptor for endless replay.
  /// @param callback Optional completion/error callback.
  /// @param callback_context Opaque callback context pointer.
  /// @return STATUS_OK on success, or a negative status code on failure.
  int configure_word_stream(const volatile std::uint32_t* destination_register,
                            const std::uint32_t* source_words,
                            std::size_t word_count, std::uint8_t trigger_source,
                            bool loop_forever, CompletionCallback callback,
                            void* callback_context);

  /// @brief Enable the configured DMAC channel.
  /// @return STATUS_OK on success, or STATUS_ERR_NOT_READY if not configured.
  int start();

  /// @brief Disable the DMAC channel and clear channel interrupt state.
  /// @return STATUS_OK.
  int stop();

  /// @brief Report whether this channel is currently marked active.
  /// @return True if the channel is configured and running.
  bool is_running() const;

  /// @brief Service this channel's pending DMAC interrupt flags.
  /// @details This function is intended to be called from DMAC_Handler().
  void handle_interrupt();

 private:
  /// @brief Initialize shared DMAC controller state once.
  /// @return STATUS_OK on success, or a negative status code on failure.
  static int initialize_controller();

  /// @brief Select an active hardware channel in the DMAC CHID register.
  /// @param channel_index Zero-based DMAC channel index.
  static void select_channel(std::uint8_t channel_index);

  /// @brief Hardware DMAC channel index owned by this object.
  std::uint8_t channel_index_;
  /// @brief True once configure_word_stream() has successfully programmed a
  /// descriptor.
  bool configured_;
  /// @brief True while transfer activity is expected for this channel.
  bool running_;
  /// @brief True when the descriptor is self-linked for endless playback.
  bool loop_forever_;
  /// @brief Optional callback invoked when completion/error interrupt is
  /// observed.
  CompletionCallback callback_;
  /// @brief User callback context returned to callback_.
  void* callback_context_;
};

}  // namespace oshal::internal

#endif /* OSHAL_SAMD21_DMAC_CHANNEL_HPP_ */
