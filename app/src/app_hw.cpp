#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "bal/ws2812_strip.hpp"
#include "oshal/debug_port.hpp"
#include "oshal/serial_port.hpp"
#include "oshal/status.h"
#include "prism_hw_executor.hpp"

namespace app::internal {

/* ========================================================================
 * PrismHwExecutor
 * ======================================================================== */

PrismHwExecutor& PrismHwExecutorInstance() {
  static PrismHwExecutor executor;
  return executor;
}

void PrismHwExecutor::Configure(bal::Led* status_led,
                                oshal::DebugPort* debug_port,
                                oshal::SerialPort* command_port) {
  status_led_ = status_led;
  debug_port_ = debug_port;
  command_port_ = command_port;
}

int PrismHwExecutor::Start() {
  if (task_.is_valid()) {
    return STATUS_OK;
  }

  oshal::TaskConfig config;
  config.name = "app_hw";
  config.setup = SetupCallback;
  config.loop = LoopCallback;
  config.context = this;
  config.stack_size_bytes = kTaskStackSizeBytes;
  config.priority = kTaskPriority;
  return oshal::TaskHandle::create(task_, config);
}

int PrismHwExecutor::PublishFrame(const SharedFrame& frame) {
  if (!task_.is_valid()) {
    return STATUS_ERR_NOT_READY;
  }

  if (task_.has_exited()) {
    int exit_code = STATUS_ERR_BACKEND;
    if (task_.exit_code(&exit_code) < 0) {
      return STATUS_ERR_BACKEND;
    }
    return exit_code;
  }

  return mailbox_.Send(&frame);
}

bool PrismHwExecutor::IsRunning() const { return task_.is_valid(); }

bool PrismHwExecutor::SetupCallback(void* context) {
  auto* self = static_cast<PrismHwExecutor*>(context);
  return (self != nullptr) && self->Setup();
}

bool PrismHwExecutor::LoopCallback(void* context) {
  auto* self = static_cast<PrismHwExecutor*>(context);
  if (self == nullptr) {
    return false;
  }
  return self->Loop();
}

bool PrismHwExecutor::Setup() {
  /*
   * Capture pointers from the configure() injection and the BAL singleton.
   * prism::initialize() already validated every resource — duplicate
   * is_ready() checks are unnecessary.
   */
  backend_strip_ = &bal::ws2812_strip();

  if (!PrintStartupBanners()) {
    return false;
  }

  return true;
}

bool PrismHwExecutor::Loop() {
  if ((backend_strip_ == nullptr) || (status_led_ == nullptr)) {
    return false;
  }

  /* Run the protocol engine — reads from the command port via the
   * StreamReader callback and transmits any queued outbound frames
   * (e.g. loopback echoes). */
  protocol_.run();

  frame_event_.WaitAny(kFrameEventMask, kIdleSleepMs);

  if (!TryApplyLatest()) {
    return false;
  }

  if (!BlinkStatusLed()) {
    return false;
  }

  return true;
}

bool PrismHwExecutor::TryApplyLatest() {
  SharedFrame frame;
  while (mailbox_.Receive(&frame)) {
    if (ApplyFrame(frame) < 0) {
      return false;
    }
  }

  return true;
}

int PrismHwExecutor::ApplyFrame(const SharedFrame& frame) {
  if (frame.led_count > backend_strip_->led_count()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t index = 0; index < frame.led_count; ++index) {
    bal::Ws2812Led* const pixel = backend_strip_->led(index);
    if ((pixel == nullptr) || !pixel->is_ready()) {
      return STATUS_ERR_DEVICE_UNAVAILABLE;
    }

    const prism::RgbColor& color = frame.colors[index];
    const int set_ret =
      pixel->set_color(bal::RgbColor{color.red, color.green, color.blue});
    if (set_ret < 0) {
      return set_ret;
    }
  }

  return backend_strip_->show();
}

bool PrismHwExecutor::PrintStartupBanners() {
  if (debug_port_->printf("DebugPort online on %s, strip on %s\n",
                          debug_port_->name(), backend_strip_->name()) < 0) {
    return false;
  }

  return true;
}

bool PrismHwExecutor::BlinkStatusLed() {
  ++blink_tick_;
  if (blink_tick_ < kBlinkHalfPeriodTicks) {
    return true;
  }

  blink_tick_ = 0U;
  return status_led_->toggle() >= 0;
}

uint32_t PrismHwExecutor::CommandPortRead(uint8_t* buffer, uint32_t length) {
  auto& exec = PrismHwExecutorInstance();
  auto* port = exec.command_port_;
  if (port == nullptr) {
    return 0U;
  }

  const int ret = port->read(buffer, static_cast<std::size_t>(length));
  return ret < 0 ? 0U : static_cast<uint32_t>(ret);
}

bool PrismHwExecutor::CommandPortWrite(const uint8_t* data, uint32_t length) {
  auto& exec = PrismHwExecutorInstance();
  auto* port = exec.command_port_;
  if (port == nullptr) {
    return false;
  }

  return port->write(data, static_cast<std::size_t>(length)) >= 0;
}

int PrismHwExecutor::DebugPortPrintf(const char* fmt, ...) {
  auto& exec = PrismHwExecutorInstance();
  auto* dbg = exec.debug_port_;
  if (dbg == nullptr) {
    return -1;
  }

  std::va_list args;
  va_start(args, fmt);
  const int ret = dbg->vprintf(fmt, args);
  va_end(args);
  return ret;
}

}  // namespace app::internal