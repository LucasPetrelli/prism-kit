#ifndef OSHAL_DEBUG_PORT_HPP_
#define OSHAL_DEBUG_PORT_HPP_

#include <cstdarg>
#include <cstddef>

namespace oshal {

/// @brief Generic debug output interface exposed by OSHAL.
/// @note The board selects the backing transport while higher layers rely on a
///     stable formatting and byte-stream API.
class DebugPort {
 public:
  DebugPort(const DebugPort&) = delete;
  DebugPort& operator=(const DebugPort&) = delete;
  virtual ~DebugPort() = default;

  /// @brief Return a human-readable diagnostic name for the transport.
  /// @return Pointer to a static string describing the transport.
  virtual const char* name() const = 0;

  /// @brief Report whether the backend for this debug port is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Write raw bytes to the debug port.
  /// @param buffer Pointer to the bytes to transmit.
  /// @param length Number of bytes to transmit.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int write(const char* buffer, std::size_t length) const = 0;

  /// @brief Format and write a message to the debug port.
  /// @param format Printf-style format string.
  /// @param args Format argument list.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int vprintf(const char* format, std::va_list args) const = 0;

  /// @brief Format and write a message to the debug port.
  /// @param format Printf-style format string.
  /// @param ... Optional format arguments.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int printf(const char* format, ...) const {
    std::va_list args;
    va_start(args, format);
    const int ret = vprintf(format, args);
    va_end(args);
    return ret;
  }

 protected:
  DebugPort() = default;
};

/// @brief Public OSHAL reference to the default debug output transport.
extern DebugPort& debug_port;

}  // namespace oshal

#endif /* OSHAL_DEBUG_PORT_HPP_ */