#pragma once

#include <cstdint>

namespace protocol {

/// @brief Protocol command tags.
///
/// Tags 0x0000–0x00FF are reserved for protocol-level commands.
/// Tags 0x0100–0xFFFF are available for application-defined commands.
enum class Tag : uint16_t {
  /// @brief Echo received data back to sender.
  /// Used for connectivity and round-trip testing.
  kLoopback = 0x0000,

  /// @brief Reserved range floor for protocol-level commands.
  kReservedMin = 0x0000,

  /// @brief Reserved range ceiling for protocol-level commands.
  kReservedMax = 0x00FF,

  /// @brief First tag available for application-defined commands.
  kUserMin = 0x0100,

  /// @brief Set a range of pixels to a single color.
  /// Payload: r, g, b, start, end (5 bytes).
  kSetMultipleColor = 0x0100,

  /// @brief Set a single pixel to a color.
  /// Payload: r, g, b, index (4 bytes).
  kSetSingleColor = 0x0101,

  /// @brief Clear all queued controller instructions.
  /// Payload: none (0 bytes).
  kResetInstructions = 0x0102,

  /// @brief Execute all queued controller instructions.
  /// Payload: none (0 bytes).
  kRun = 0x0103,
};

}  // namespace protocol
