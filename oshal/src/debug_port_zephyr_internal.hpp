#ifndef OSHAL_DEBUG_PORT_ZEPHYR_INTERNAL_HPP_
#define OSHAL_DEBUG_PORT_ZEPHYR_INTERNAL_HPP_

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <cstdarg>
#include <cstddef>

#include "oshal/debug_port.hpp"

namespace oshal::internal {

/// @brief Zephyr-backed debug port implementation bound to a single device.
class ZephyrDebugPort final : public DebugPort {
 public:
  /// @brief Construct a Zephyr-backed debug port object.
  /// @param port_name Static diagnostic name for the debug transport.
  /// @param device Zephyr device that owns the transport.
  ZephyrDebugPort(const char* port_name, const device* device);

  const char* name() const override;
  bool is_ready() const override;
  int write(const char* buffer, std::size_t length) const override;
  int vprintf(const char* format, std::va_list args) const override;

 private:
  int enable_blocking_poll_mode() const;

  /// @brief Static diagnostic name for this transport instance.
  const char* name_;
  /// @brief Zephyr device selected as the backing console transport.
  const device* device_;
  /// @brief Tracks whether the one-time poll-mode configuration was attempted.
  mutable bool blocking_poll_mode_checked_;
  /// @brief True when the device accepted the blocking poll-mode configuration.
  mutable bool blocking_poll_mode_enabled_;
};

}  // namespace oshal::internal

#endif /* OSHAL_DEBUG_PORT_ZEPHYR_INTERNAL_HPP_ */