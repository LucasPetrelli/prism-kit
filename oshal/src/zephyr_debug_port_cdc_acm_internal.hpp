#ifndef OSHAL_ZEPHYR_DEBUG_PORT_CDC_ACM_INTERNAL_HPP_
#define OSHAL_ZEPHYR_DEBUG_PORT_CDC_ACM_INTERNAL_HPP_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "oshal/debug_port.hpp"

namespace oshal::internal {

/// @brief CDC ACM-specific debug port implementation using the TX FIFO/IRQ API.
class ZephyrCdcAcmDebugPort final : public DebugPort {
 public:
  /// @brief Construct a CDC ACM-backed debug port object.
  /// @param port_name Static diagnostic name for the debug transport.
  /// @param device Zephyr device that owns the CDC ACM UART transport.
  ZephyrCdcAcmDebugPort(const char* port_name, const device* device);

  const char* name() const override;
  bool is_ready() const override;
  int write(const char* buffer, std::size_t length) const override;
  int vprintf(const char* format, std::va_list args) const override;

 private:
  static constexpr std::size_t kTxQueueCapacity = 512U;

  /// @brief Scratch buffer used to batch cbprintf output into transport writes.
  struct FormatBuffer {
    /// @brief Owning CDC ACM debug port instance used for flushing chunks.
    const ZephyrCdcAcmDebugPort* port;
    /// @brief Small temporary buffer that accumulates formatted characters.
    char data[64];
    /// @brief Number of valid bytes currently stored in @ref data.
    std::size_t length;
    /// @brief Sticky STATUS_* result captured while formatting or flushing.
    int status;
  };

  static void tx_irq_callback(const device* device, void* user_data);
  static int format_char_out(int c, void* context);

  int flush_format_buffer(FormatBuffer* buffer) const;
  void service_tx_fifo() const;

  /// @brief Static diagnostic name for this CDC ACM transport instance.
  const char* name_;
  /// @brief Zephyr CDC ACM UART device selected as the backing transport.
  const device* device_;
  /// @brief Serializes writer-side queue insertion so message chunks stay
  /// contiguous when space allows.
  mutable k_mutex write_mutex_;
  /// @brief Protects shared queue state touched by writer and IRQ callback.
  mutable k_spinlock state_lock_;
  /// @brief Ring buffer that absorbs short bursts without blocking callers.
  mutable std::array<std::uint8_t, kTxQueueCapacity> tx_queue_;
  /// @brief Next byte index to drain from @ref tx_queue_.
  mutable std::size_t tx_head_;
  /// @brief Next byte index to append into @ref tx_queue_.
  mutable std::size_t tx_tail_;
  /// @brief Number of queued bytes waiting to enter the CDC ACM FIFO.
  mutable std::size_t tx_size_;
  /// @brief Guard that prevents concurrent queue-drain loops from racing.
  mutable bool tx_service_active_;
  /// @brief Cumulative count of bytes dropped because @ref tx_queue_ was full.
  mutable std::uint32_t dropped_bytes_;
  /// @brief True when the UART IRQ callback registration succeeded at startup.
  bool tx_irq_callback_bound_;
};

}  // namespace oshal::internal

#endif /* OSHAL_ZEPHYR_DEBUG_PORT_CDC_ACM_INTERNAL_HPP_ */