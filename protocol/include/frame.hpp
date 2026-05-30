#pragma once

#include <cstddef>
#include <cstdint>

#include "tags.hpp"


namespace protocol {

/// @brief Sync byte that marks the start of every frame on the wire.
constexpr uint8_t kSyncByte = 0xAA;

/// @brief Escape byte used to stuff special bytes that appear in the payload.
constexpr uint8_t kEscapeByte = 0xBB;

/// @brief XOR mask applied to a byte when it is escaped.
/// On the wire: kEscapeByte followed by (original ^ kEscapeXor).
constexpr uint8_t kEscapeXor = 0x01;

/// @brief Wire-level header size in bytes (tag + length, little-endian).
constexpr size_t kHeaderSize = 4;

/// @brief Checksum size in bytes (XOR over tag + length + data).
constexpr size_t kChecksumSize = 1;

/// @brief Maximum data payload length in bytes.
constexpr size_t kMaxFrameDataLength = 256;

/// @brief RX buffer size: header + max data.
constexpr size_t kRxBufferSize = kHeaderSize + kMaxFrameDataLength;

#pragma pack(push, 1)

/// @brief On-wire frame header layout (tag + length, little-endian).
///
/// This struct is only used for serialization; the wire format always
/// starts with a kSyncByte before these fields.
struct FrameHeader {
  uint16_t tag;     ///< Command tag.
  uint16_t length;  ///< Data payload length in bytes.
};

#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 4, "FrameHeader must be 4 bytes");

/// @brief Represents a protocol frame for construction and inspection.
///
/// A Frame holds a tag, a pointer to payload data, and the payload length.
/// It does not own the data — the caller must ensure the data outlives the
/// frame until it is transmitted.
class Frame {
 public:
  /// @brief Create a frame with the given tag and data.
  /// @param tag   Command tag.
  /// @param data  Pointer to payload data. May be nullptr if length is 0.
  /// @param length Payload length in bytes. Must be <= kMaxFrameDataLength.
  /// @return A valid Frame, or an invalid frame (is_valid() == false) on error.
  static Frame create(Tag tag, const uint8_t* data, uint16_t length);

  /// @brief Parse a received frame header from wire bytes.
  /// @param wire_data Pointer to 4-byte header (tag LE, length LE).
  /// @return Parsed FrameHeader.
  static FrameHeader parse_header(const uint8_t* wire_data);

  /// @brief Default-construct an empty, invalid frame.
  Frame() = default;

  /// @return The frame's tag.
  Tag tag() const { return tag_; }

  /// @return Pointer to payload data. nullptr if empty or invalid.
  const uint8_t* data() const { return data_; }

  /// @return Payload length in bytes.
  uint16_t length() const { return length_; }

  /// @return True if the frame contains valid data.
  /// A zero-length frame is valid even with a null data pointer.
  bool is_valid() const { return length_ == 0 || data_ != nullptr; }

 private:
  Frame(Tag tag, const uint8_t* data, uint16_t length);

  Tag tag_{};
  const uint8_t* data_{nullptr};
  uint16_t length_{0};
};

}  // namespace protocol
