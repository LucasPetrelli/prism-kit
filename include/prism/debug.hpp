#ifndef PRISM_DEBUG_HPP_
#define PRISM_DEBUG_HPP_

#include <cstdarg>

namespace prism {

/// @brief Abstract debug output interface for formatted diagnostic messages.
///
/// Implementations write to a debug transport (serial, CDC ACM, RTT, etc.).
/// Callers check for a non-null instance before calling any method, so it is
/// safe to leave unset.
class Debug {
 public:
  Debug(const Debug&) = delete;
  Debug& operator=(const Debug&) = delete;
  virtual ~Debug() = default;

  /// @brief Write a formatted message through the debug transport.
  /// @param format Printf-style format string.
  /// @param args Variable-argument list.
  virtual void Vprintf(const char* format, std::va_list args) = 0;

  /// @brief Convenience wrapper that forwards variadic arguments to Vprintf.
  /// @param format Printf-style format string.
  /// @param ... Variadic arguments matching the format string.
  void Printf(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    Vprintf(format, args);
    va_end(args);
  }

 protected:
  Debug() = default;
};

}  // namespace prism

#endif /* PRISM_DEBUG_HPP_ */
