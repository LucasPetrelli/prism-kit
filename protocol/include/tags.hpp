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
};

}  // namespace protocol
