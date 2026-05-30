#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "frame.hpp"
#include "tags.hpp"

namespace protocol {

/// @brief Maximum number of registered frame handlers.
constexpr size_t kMaxHandlers = 8;

/// @brief Callback to read bytes from the transport stream.
/// @param buffer Destination buffer.
/// @param length Maximum number of bytes to read.
/// @return Number of bytes actually read (0 if none available).
using StreamReader = uint32_t (*)(uint8_t* buffer, uint32_t length);

/// @brief Callback to write bytes to the transport stream.
/// @param data   Data to write.
/// @param length Number of bytes to write.
/// @return true if all bytes were written successfully, false on error.
using StreamWriter = bool (*)(const uint8_t* data, uint32_t length);

/// @brief Callback invoked when a complete frame is received for a registered
/// tag.
/// @param context Opaque pointer passed to add_handler().
/// @param data    Pointer to the frame's payload data.
/// @param length  Payload length in bytes.
using FrameHandler = void (*)(void* context, const uint8_t* data,
                              uint16_t length);

/// @brief Configuration for a Protocol instance.
struct ProtocolConfig {
  StreamReader read;   ///< Non-blocking stream read callback.
  StreamWriter write;  ///< Non-blocking stream write callback.
};

/// @brief TLV protocol engine with sync-byte framing and XOR checksum.
///
/// ## Thread Safety
/// This class is not reentrant. All calls to run(), send(), and
/// add_handler() must come from a single task or be externally serialized.
///
/// ## State Machine
/// The parser inside run() cycles through these states:
///
///   1. kWaitingForSync  — scan the stream for kSyncByte
///   2. kReadingHeader   — accumulate 4 header bytes, validate length
///   3. kReadingData     — accumulate `length` data bytes
///   4. kReadingChecksum — read the checksum byte; on match, dispatch
///                          the frame immediately and reset the parser.
/// If any state detects an error (bad length, checksum mismatch), the
/// parser discards the partial frame and returns to kWaitingForSync.
///
/// ## Wire Format
///
///     | Sync (1) | Tag (2 LE) | Length (2 LE) | Data (Length) | Checksum (1) |
///
/// ### Checksum
/// The checksum is the XOR of all bytes in Tag (2), Length (2),
/// and Data (Length). The checksum byte is transmitted as-is.
class Protocol {
 public:
  /// @brief Construct a protocol instance.
  /// @param config Stream callbacks.  Either callback may be null to
  ///               disable that direction (RX-only or TX-only operation).
  ///               If both are null the instance is valid but idle.
  explicit Protocol(const ProtocolConfig& config);

  /// @brief Register a handler for a specific tag.
  /// @param tag     The tag to handle (must not already be registered).
  /// @param handler The handler callback (must be non-null).  The
  ///                Protocol instance is passed as the handler's context.
  /// @return true on success, false if the table is full, the tag is
  ///         already registered, or handler is null.
  bool add_handler(Tag tag, FrameHandler handler);

  /// @brief Queue a frame for transmission.
  /// @param frame The frame to send. Must be valid (is_valid() == true).
  /// @return true if the frame was queued, false if a frame is already
  ///         pending transmission.
  /// @note Only one frame can be queued at a time. The frame's data
  ///       is copied into an internal buffer, so the caller may free or
  ///       reuse the original data immediately after this call returns.
  bool send(const Frame& frame);

  /// @brief Process one iteration of the protocol state machine.
  ///
  /// Reads available bytes from the stream, parses complete frames,
  /// dispatches them to registered handlers, and transmits any queued
  /// outgoing frame. Call this periodically from the main task loop.
  void run();

  /// @brief Feed received bytes directly into the protocol parser.
  /// @param data   Pointer to received bytes.
  /// @param length Number of bytes received.
  /// @note Use this instead of the StreamReader callback for push-based
  ///       data delivery. Bytes are consumed on the next call to run().
  void feed(const uint8_t* data, uint32_t length);

 private:
  enum class State : uint8_t {
    kWaitingForSync,
    kReadingHeader,
    kReadingData,
    kReadingChecksum,
  };

  struct HandlerEntry {
    Tag tag{};
    FrameHandler handler{nullptr};
    void* context{nullptr};
  };

  /// @brief Built-in loopback handler (registered for kLoopback by default).
  static void loopback_impl_(void* context, const uint8_t* data,
                             uint16_t length);

  /// @brief Read a single raw byte from the stream or fed buffer.
  /// @param out Set to the byte read on success.
  /// @return true if a byte was available, false if no data.
  bool read_raw_byte(uint8_t& out);

  /// @brief Write a frame to the stream with sync and checksum.
  /// @param header      Pointer to 4-byte header (tag LE, length LE).
  /// @param data        Pointer to payload data.
  /// @param data_length Payload length in bytes.
  /// @return true if all bytes were written successfully.
  bool write_frame_wire(const uint8_t* header, const uint8_t* data,
                        uint16_t data_length);

  /// @brief Compute XOR checksum over header bytes + data bytes.
  /// @param header     Pointer to 4-byte header (tag LE, length LE).
  /// @param data       Pointer to payload data.
  /// @param data_length Payload length in bytes.
  /// @return XOR checksum byte.
  static uint8_t compute_checksum(const uint8_t* header, const uint8_t* data,
                                  uint16_t data_length);

  /// @brief Reset the parser to the waiting-for-sync state.
  void reset_parser();

  /// @brief Dispatch a fully received frame to the matching handler.
  void dispatch_frame();

  /// @brief Store one raw byte into the output reference.
  /// @param raw  The raw byte from the stream.
  /// @param out  Set to raw.
  static void consume_byte(uint8_t raw, uint8_t& out);

  // --- Configuration ---
  StreamReader read_{nullptr};
  StreamWriter write_{nullptr};

  // --- Handler table ---
  std::array<HandlerEntry, kMaxHandlers> handlers_{};
  uint8_t handler_count_{0};

  // --- Read-ahead ---
  /// Batched-read buffer so the parser amortises StreamReader calls.
  uint8_t readahead_[16]{};
  uint8_t readahead_idx_{0};
  uint8_t readahead_cnt_{0};

  // --- RX parser state ---
  State state_{State::kWaitingForSync};
  uint8_t rx_buffer_[kRxBufferSize]{};
  uint16_t rx_index_{0};
  uint16_t rx_data_length_{0};
  uint16_t rx_tag_{0};

  // --- TX state ---
  /// Owned buffer that holds the data payload queued for transmission.
  uint8_t tx_data_buffer_[kMaxFrameDataLength]{};
  /// If non-null a frame is pending; points into tx_data_buffer_.
  const uint8_t* tx_data_{nullptr};
  uint16_t tx_length_{0};
  uint16_t tx_tag_{0};

  // --- Fed data ---
  const uint8_t* fed_data_{nullptr};
  uint32_t fed_length_{0};
  uint32_t fed_offset_{0};
};

}  // namespace protocol
