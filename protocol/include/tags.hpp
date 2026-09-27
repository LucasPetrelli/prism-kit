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

  /// @brief Removed: kDelay = 0x0104.  Marks now replace explicit delay
  ///     instructions.

  /// @brief Set a single pixel from an HSV color.
  /// Payload: h, s, v, index (4 bytes).
  kSetSingleColorHsv = 0x0105,

  /// @brief Set a range of pixels from an HSV color.
  /// Payload: h, s, v, start, end (5 bytes).
  kSetMultipleColorHsv = 0x0106,

  /// @brief Set a range of pixels to one color in a group.
  /// Payload: mark:u16, r, g, b, start, end, group_id:u32 (11 bytes).
  kSetMultipleColorGrouped = 0x0107,

  /// @brief Set a single pixel to one color in a group.
  /// Payload: mark:u16, r, g, b, index, group_id:u32 (10 bytes).
  kSetSingleColorGrouped = 0x0108,

  /// @brief Set a single pixel from an HSV color in a group.
  /// Payload: mark:u16, h, s, v, index, group_id:u32 (10 bytes).
  kSetSingleColorHsvGrouped = 0x0109,

  /// @brief Set a range of pixels from an HSV color in a group.
  /// Payload: mark:u16, h, s, v, start, end, group_id:u32 (11 bytes).
  kSetMultipleColorHsvGrouped = 0x010A,

  /// @brief Schedule a child-group activation on a parent timeline.
  /// Payload: parent_group_id:u32, mark:u16, target_group_id:u32,
  ///     additional_repeats:u16 (12 bytes).
  kRunGroup = 0x010B,

  /// @brief Start or replay the retained program with a root repeat policy.
  /// Payload: root_additional_repeats:u16 (2 bytes).
  kStart = 0x010C,
};

}  // namespace protocol
