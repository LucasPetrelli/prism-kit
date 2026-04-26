#include <zephyr/drivers/uart.h>
#include <zephyr/sys/cbprintf.h>

#include <climits>

#include "oshal/status.h"
#include "zephyr_debug_port_cdc_acm_internal.hpp"

namespace oshal::internal {

ZephyrCdcAcmDebugPort::ZephyrCdcAcmDebugPort(const char* port_name,
                                             const device* device)
    : name_(port_name),
      device_(device),
      tx_queue_({}),
      tx_head_(0U),
      tx_tail_(0U),
      tx_size_(0U),
      tx_service_active_(false),
      dropped_bytes_(0U),
      tx_irq_callback_bound_(false) {
  /*
   * The CDC ACM backend keeps its own small synchronization model because the
   * USB class driver does not expose a simple blocking uart_tx() API. Instead
   * we queue bytes into the CDC ACM UART FIFO whenever it reports TX-ready and
   * return as soon as the current request is accepted into the local queue.
   */
  k_mutex_init(&write_mutex_);
  /*
   * If this callback registration fails there is no useful FIFO-driven
   * transport path left for this backend, so later writes will fail fast with
   * STATUS_ERR_BACKEND instead of hanging while waiting for a completion event
   * that can never be signaled.
   */
  tx_irq_callback_bound_ =
    uart_irq_callback_user_data_set(
      device_, &ZephyrCdcAcmDebugPort::tx_irq_callback, this) == 0;
}

const char* ZephyrCdcAcmDebugPort::name() const { return name_; }

bool ZephyrCdcAcmDebugPort::is_ready() const {
  return (device_ != nullptr) && device_is_ready(device_);
}

int ZephyrCdcAcmDebugPort::write(const char* buffer, std::size_t length) const {
  if (buffer == nullptr) {
    return length == 0U ? STATUS_OK : STATUS_ERR_INVALID_ARGUMENT;
  }

  if (!is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (!tx_irq_callback_bound_) {
    return STATUS_ERR_BACKEND;
  }

  if (length == 0U) {
    return STATUS_OK;
  }

  /*
   * Copy bytes into the bounded queue and return immediately. This keeps APP
   * and BAL tasks from blocking on host-side USB consumption when no terminal
   * is attached, while still letting the IRQ-driven CDC ACM path drain queued
   * bytes opportunistically.
   *
   * Any bytes that do not fit are intentionally dropped so producers keep
   * running even under sustained USB backpressure.
   */
  k_mutex_lock(&write_mutex_, K_FOREVER);
  std::size_t accepted = 0U;
  std::size_t dropped = 0U;
  {
    const k_spinlock_key_t key = k_spin_lock(&state_lock_);
    while ((accepted < length) && (tx_size_ < tx_queue_.size())) {
      tx_queue_[tx_tail_] = static_cast<std::uint8_t>(buffer[accepted]);
      tx_tail_ = (tx_tail_ + 1U) % tx_queue_.size();
      ++tx_size_;
      ++accepted;
    }

    dropped = length - accepted;
    if (dropped > 0U) {
      const std::uint32_t remaining = UINT32_MAX - dropped_bytes_;
      dropped_bytes_ = dropped > static_cast<std::size_t>(remaining)
                         ? UINT32_MAX
                         : static_cast<std::uint32_t>(dropped_bytes_ + dropped);
    }
    k_spin_unlock(&state_lock_, key);
  }

  /*
   * Drain work is performed from the UART IRQ callback path. For the
   * interrupt-driven UART API, uart_irq_update()/uart_irq_tx_ready()/
   * uart_fifo_fill() are ISR-context primitives, so write() only needs to
   * enable TX IRQ and return.
   */
  uart_irq_tx_enable(device_);
  k_mutex_unlock(&write_mutex_);

  if (dropped > 0U) {
    /*
     * Dropped-byte accounting is kept so this path can grow diagnostics later
     * without changing the queueing logic. The public DebugPort contract stays
     * best-effort, so writes report success after giving the transport a
     * chance to queue what fit, even when host-side backpressure means the
     * current call could not stage any bytes immediately.
     */
  }
  return STATUS_OK;
}

int ZephyrCdcAcmDebugPort::vprintf(const char* format,
                                   std::va_list args) const {
  if (format == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if (!is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  /*
   * The CDC ACM backend deliberately formats into fixed-size chunks instead of
   * sending one byte at a time through the formatter callback. That gives the
   * transport-specific write path a chance to hand larger spans to
   * uart_fifo_fill and makes the whole path meaningfully different from the
   * generic poll_out() fallback.
   */
  FormatBuffer buffer{this, {}, 0U, STATUS_OK};
  const int format_result = z_cbvprintf_impl(
    reinterpret_cast<cbprintf_cb>(&ZephyrCdcAcmDebugPort::format_char_out),
    &buffer, format, args, 0U);
  if (format_result < 0) {
    return STATUS_ERR_BACKEND;
  }

  return flush_format_buffer(&buffer);
}

void ZephyrCdcAcmDebugPort::tx_irq_callback(const device* device,
                                            void* user_data) {
  ARG_UNUSED(device);
  /*
   * Zephyr's interrupt-driven UART callback API delivers user_data, which lets
   * the backend bounce directly back into its instance method without relying
   * on any file-global singleton state.
   */
  auto* self = static_cast<ZephyrCdcAcmDebugPort*>(user_data);
  self->service_tx_fifo();
}

int ZephyrCdcAcmDebugPort::format_char_out(int c, void* context) {
  auto* buffer = static_cast<FormatBuffer*>(context);

  if (buffer->status < 0) {
    return buffer->status;
  }

  /*
   * Accumulate a short run of formatted bytes locally. Once the scratch buffer
   * is full, flush it through the CDC ACM transport-specific write path and
   * keep formatting. This keeps the formatter simple while still avoiding
   * per-byte transport calls.
   */
  buffer->data[buffer->length++] = static_cast<char>(c);
  if (buffer->length == sizeof(buffer->data)) {
    buffer->status = buffer->port->flush_format_buffer(buffer);
    if (buffer->status < 0) {
      return buffer->status;
    }
  }

  return c;
}

int ZephyrCdcAcmDebugPort::flush_format_buffer(FormatBuffer* buffer) const {
  if (buffer->status < 0) {
    return buffer->status;
  }

  if (buffer->length == 0U) {
    return STATUS_OK;
  }

  /*
   * Reuse write() so both raw writes and formatted writes share exactly the
   * same FIFO-draining, non-blocking transport logic. That keeps the CDC
   * ACM behavior consistent no matter how bytes enter the backend.
   */
  buffer->status = write(buffer->data, buffer->length);
  buffer->length = 0U;
  return buffer->status;
}

void ZephyrCdcAcmDebugPort::service_tx_fifo() const {
  /*
   * This function runs from two places: once from the writer thread immediately
   * after TX is enabled, and again from the CDC ACM UART callback whenever the
   * driver says the FIFO can accept more bytes. In both cases the job is the
   * same: copy as much of the pending byte span into the FIFO as the driver
   * will currently accept, then either wait for another callback or complete
   * the write.
   */
  {
    const k_spinlock_key_t key = k_spin_lock(&state_lock_);
    if (tx_service_active_) {
      k_spin_unlock(&state_lock_, key);
      return;
    }
    tx_service_active_ = true;
    k_spin_unlock(&state_lock_, key);
  }

  while (true) {
    if (uart_irq_update(device_) <= 0) {
      break;
    }

    if (uart_irq_tx_ready(device_) <= 0) {
      /* No FIFO space right now; another callback will resume draining later.
       */
      break;
    }

    std::size_t head;
    std::size_t requested;
    {
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      if (tx_size_ == 0U) {
        k_spin_unlock(&state_lock_, key);
        break;
      }

      head = tx_head_;
      const std::size_t contiguous = tx_queue_.size() - tx_head_;
      requested = tx_size_ < contiguous ? tx_size_ : contiguous;
      k_spin_unlock(&state_lock_, key);
    }

    const int requested_int = requested > static_cast<std::size_t>(INT_MAX)
                                ? INT_MAX
                                : static_cast<int>(requested);
    const int wrote = uart_fifo_fill(device_, &tx_queue_[head], requested_int);
    if (wrote < 0) {
      /* Treat any FIFO API failure as a transport backend failure for OSHAL. */
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      tx_head_ = 0U;
      tx_tail_ = 0U;
      tx_size_ = 0U;
      k_spin_unlock(&state_lock_, key);
      break;
    }

    if (wrote == 0) {
      /* Driver reported TX-ready but accepted nothing; wait for the next turn.
       */
      break;
    }

    {
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      const std::size_t consumed = static_cast<std::size_t>(wrote);
      tx_head_ = (tx_head_ + consumed) % tx_queue_.size();
      tx_size_ -= consumed;
      k_spin_unlock(&state_lock_, key);
    }
  }

  {
    const k_spinlock_key_t key = k_spin_lock(&state_lock_);
    const bool queue_empty = tx_size_ == 0U;
    tx_service_active_ = false;
    k_spin_unlock(&state_lock_, key);
    if (queue_empty) {
      uart_irq_tx_disable(device_);
    }
  }
}

}  // namespace oshal::internal