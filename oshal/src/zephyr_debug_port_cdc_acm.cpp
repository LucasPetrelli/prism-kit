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
      pending_buffer_(nullptr),
      pending_length_(0U),
      pending_status_(STATUS_OK),
      tx_irq_callback_bound_(false) {
  /*
   * The CDC ACM backend keeps its own small synchronization model because the
   * USB class driver does not expose a simple blocking uart_tx() API. Instead
   * we queue bytes into the CDC ACM UART FIFO whenever it reports TX-ready and
   * let the caller sleep until the current request is drained.
   */
  k_mutex_init(&write_mutex_);
  k_sem_init(&write_complete_, 0U, 1U);

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
   * Serialize callers so the backend only ever owns one pending write request
   * at a time. That keeps the state machine simple: one caller publishes one
   * byte span, the callback drains it, and the caller wakes when that span
   * reaches zero remaining bytes.
   *
   * Without this mutex, concurrent writers would race to replace
   * pending_buffer_/pending_length_ and the callback would have no reliable way
   * to know which caller owns the current completion semaphore.
   */
  k_mutex_lock(&write_mutex_, K_FOREVER);
  k_sem_reset(&write_complete_);

  {
    /*
     * The callback path advances these fields as it feeds bytes into the CDC
     * ACM TX FIFO, so publish them under the spinlock before enabling TX IRQ.
     */
    const k_spinlock_key_t key = k_spin_lock(&state_lock_);
    pending_buffer_ = reinterpret_cast<const std::uint8_t*>(buffer);
    pending_length_ = length;
    pending_status_ = STATUS_OK;
    k_spin_unlock(&state_lock_, key);
  }

  /*
   * CDC ACM does not expose uart_tx(), but it does provide a TX FIFO that is
   * serviced from the USB workqueue. Enabling TX IRQ causes the driver to
   * invoke the callback whenever more FIFO space is available. That gives this
   * backend a transport-aware blocking write path: the caller sleeps on a
   * semaphore while the workqueue and callback path push chunks through the CDC
   * ACM FIFO.
   *
   * service_tx_fifo() is called once immediately so the current thread can fill
   * any already-available FIFO space without waiting for the next callback.
   */
  uart_irq_tx_enable(device_);
  service_tx_fifo();

  /*
   * Completion means "the whole pending request has been copied into the CDC
   * ACM FIFO path", not necessarily "the host application has consumed every
   * byte". That matches the contract of most serial write APIs and keeps the
   * backend from pretending it can observe end-to-end host consumption.
   */
  (void)k_sem_take(&write_complete_, K_FOREVER);

  int status;
  {
    /*
     * Tear down the published request state before releasing the writer mutex
     * so the next caller always starts from a clean slate.
     */
    const k_spinlock_key_t key = k_spin_lock(&state_lock_);
    status = pending_status_;
    pending_buffer_ = nullptr;
    pending_length_ = 0U;
    k_spin_unlock(&state_lock_, key);
  }

  k_mutex_unlock(&write_mutex_);
  return status;
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
   * same FIFO-draining, semaphore-blocking transport logic. That keeps the CDC
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
  while (true) {
    if (uart_irq_update(device_) <= 0) {
      return;
    }

    if (uart_irq_tx_ready(device_) <= 0) {
      /* No FIFO space right now; another callback will resume draining later.
       */
      return;
    }

    const std::uint8_t* buffer;
    std::size_t remaining;
    {
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      buffer = pending_buffer_;
      remaining = pending_length_;
      if (pending_status_ < 0 || remaining == 0U) {
        /* Either an error already occurred or this request is fully drained. */
        k_spin_unlock(&state_lock_, key);
        uart_irq_tx_disable(device_);
        k_sem_give(&write_complete_);
        return;
      }
      k_spin_unlock(&state_lock_, key);
    }

    const int requested = remaining > static_cast<std::size_t>(INT_MAX)
                            ? INT_MAX
                            : static_cast<int>(remaining);
    const int wrote = uart_fifo_fill(device_, buffer, requested);
    if (wrote < 0) {
      /* Treat any FIFO API failure as a transport backend failure for OSHAL. */
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      pending_status_ = STATUS_ERR_BACKEND;
      k_spin_unlock(&state_lock_, key);
      uart_irq_tx_disable(device_);
      k_sem_give(&write_complete_);
      return;
    }

    if (wrote == 0) {
      /* Driver reported TX-ready but accepted nothing; wait for the next turn.
       */
      return;
    }

    bool completed = false;
    {
      const k_spinlock_key_t key = k_spin_lock(&state_lock_);
      pending_buffer_ += wrote;
      pending_length_ -= static_cast<std::size_t>(wrote);
      completed = pending_length_ == 0U;
      k_spin_unlock(&state_lock_, key);
    }

    if (completed) {
      /* Entire request is handed off into the FIFO path; wake the blocked
       * writer. */
      uart_irq_tx_disable(device_);
      k_sem_give(&write_complete_);
      return;
    }
  }
}

}  // namespace oshal::internal