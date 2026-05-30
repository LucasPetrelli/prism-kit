#include "protocol.hpp"

#include <cstring>

namespace protocol {

// ============================================================================
// Frame
// ============================================================================

Frame::Frame(Tag tag, const uint8_t* data, uint16_t length)
    : tag_(tag), data_(data), length_(length) {}

Frame Frame::create(Tag tag, const uint8_t* data, uint16_t length) {
  if (length > kMaxFrameDataLength) {
    return Frame{};
  }
  if (length > 0 && !data) {
    return Frame{};
  }
  return Frame{tag, data, length};
}

FrameHeader Frame::parse_header(const uint8_t* wire_data) {
  FrameHeader header{};
  if (!wire_data) {
    return header;
  }
  // Little-endian decode: tag at [0..1], length at [2..3].
  header.tag = static_cast<uint16_t>(wire_data[0]) |
               (static_cast<uint16_t>(wire_data[1]) << 8);
  header.length = static_cast<uint16_t>(wire_data[2]) |
                  (static_cast<uint16_t>(wire_data[3]) << 8);
  return header;
}

// ============================================================================
// Protocol — construction
// ============================================================================

Protocol::Protocol(const ProtocolConfig& config)
    : read_(config.read), write_(config.write) {
  // Tag 0x0000 (kLoopback) is always registered as the first handler entry.
  handlers_[0] = {Tag::kLoopback, &Protocol::loopback_impl_, this};
  handler_count_ = 1;
}

// ============================================================================
// Protocol — handler registration
// ============================================================================

bool Protocol::add_handler(Tag tag, FrameHandler handler) {
  if (!handler) {
    return false;
  }
  if (handler_count_ >= kMaxHandlers) {
    return false;
  }
  // Reject duplicate tags (also catches attempts to re-register kLoopback).
  for (uint8_t i = 0; i < handler_count_; ++i) {
    if (handlers_[i].tag == tag) {
      return false;
    }
  }

  handlers_[handler_count_] = {tag, handler, this};
  ++handler_count_;
  return true;
}

// ============================================================================
// Protocol — send
// ============================================================================

bool Protocol::send(const Frame& frame) {
  if (!frame.is_valid()) {
    return false;
  }
  if (tx_data_) {
    return false;
  }

  tx_tag_ = static_cast<uint16_t>(frame.tag());
  tx_length_ = frame.length();
  if (frame.data() && tx_length_ > 0) {
    // Copy into internal buffer so the caller can free/reuse their data.
    std::memcpy(tx_data_buffer_, frame.data(), tx_length_);
  }
  tx_data_ = tx_data_buffer_;
  return true;
}

// ============================================================================
// Protocol — feed
// ============================================================================

void Protocol::feed(const uint8_t* data, uint32_t length) {
  // Treat null data with nonzero length as a caller error — drop the feed.
  if (!data) {
    length = 0;
  }
  fed_data_ = data;
  fed_length_ = length;
  fed_offset_ = 0;
}

// ============================================================================
// Protocol — run (state machine)
// ============================================================================

void Protocol::run() {
  // ── TX phase: transmit pending frame ──
  if (tx_data_) {
    uint8_t header[4];
    header[0] = static_cast<uint8_t>(tx_tag_ & 0xFF);
    header[1] = static_cast<uint8_t>((tx_tag_ >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>(tx_length_ & 0xFF);
    header[3] = static_cast<uint8_t>((tx_length_ >> 8) & 0xFF);

    if (write_frame_wire(header, tx_data_, tx_length_)) {
      tx_data_ = nullptr;
    }
    // If write fails, retry on the next run() call.
  }

  // ── RX phase: parse incoming bytes ──
  uint8_t raw;
  while (read_raw_byte(raw)) {
    switch (state_) {
      // ---------------------------------------------------------------
      case State::kWaitingForSync:
        if (raw == kSyncByte) {
          reset_parser();
          state_ = State::kReadingHeader;
        }
        // Discard everything else while searching for sync.
        break;

      // ---------------------------------------------------------------
      case State::kReadingHeader: {
        uint8_t byte;
        consume_byte(raw, byte);

        rx_buffer_[rx_index_] = byte;
        ++rx_index_;

        if (rx_index_ >= kHeaderSize) {
          FrameHeader hdr = Frame::parse_header(rx_buffer_);
          if (hdr.length > kMaxFrameDataLength) {
            reset_parser();
            break;
          }
          rx_tag_ = hdr.tag;
          rx_data_length_ = hdr.length;
          rx_index_ = 0;

          if (rx_data_length_ == 0) {
            state_ = State::kReadingChecksum;
          } else {
            state_ = State::kReadingData;
          }
        }
        break;
      }

      // ---------------------------------------------------------------
      case State::kReadingData: {
        uint8_t byte;
        consume_byte(raw, byte);

        rx_buffer_[kHeaderSize + rx_index_] = byte;
        ++rx_index_;

        if (rx_index_ >= rx_data_length_) {
          state_ = State::kReadingChecksum;
        }
        break;
      }

      // ---------------------------------------------------------------
      case State::kReadingChecksum: {
        uint8_t received_checksum;
        consume_byte(raw, received_checksum);

        uint8_t expected = compute_checksum(
          rx_buffer_, rx_buffer_ + kHeaderSize, rx_data_length_);

        if (received_checksum == expected) {
          dispatch_frame();
          reset_parser();
        } else {
          // Checksum mismatch — discard frame and resync.
          reset_parser();
        }
        break;
      }
    }
  }

  // Clear fed data after processing; feed() must be called again for new
  // push-based data.
  fed_data_ = nullptr;
  fed_length_ = 0;
  fed_offset_ = 0;
}

// ============================================================================
// Protocol — internal helpers
// ============================================================================

bool Protocol::read_raw_byte(uint8_t& out) {
  // Consume push-fed data first.
  if (fed_data_ && fed_offset_ < fed_length_) {
    out = fed_data_[fed_offset_];
    ++fed_offset_;
    return true;
  }
  // Serve from the read-ahead cache when possible.
  if (readahead_idx_ < readahead_cnt_) {
    out = readahead_[readahead_idx_];
    ++readahead_idx_;
    return true;
  }
  // Refill the cache with one multi-byte call to the transport.
  if (read_) {
    uint32_t n = read_(readahead_, sizeof(readahead_));
    readahead_cnt_ =
      (n > sizeof(readahead_)) ? sizeof(readahead_) : static_cast<uint8_t>(n);
    if (readahead_cnt_ > 0) {
      readahead_idx_ = 1;
      out = readahead_[0];
      return true;
    }
  }
  return false;
}

bool Protocol::write_frame_wire(const uint8_t* header, const uint8_t* data,
                                uint16_t data_length) {
  if (!write_) {
    return false;
  }

  // Assemble the escaped frame in chunks so the transport receives at most
  // a handful of write_() calls instead of one per byte.
  constexpr size_t kChunkSize = 64;
  uint8_t chunk[kChunkSize];
  size_t fill = 0;

  // Flush the current chunk to the transport.  Returns true on success.
  auto flush = [&]() -> bool {
    if (fill == 0) return true;
    bool ok = write_(chunk, static_cast<uint32_t>(fill));
    fill = 0;
    return ok;
  };

  // Emit one raw byte into the chunk.
  auto emit = [&](uint8_t b) -> bool {
    if (fill + 1 > kChunkSize) {
      if (!flush()) return false;
    }
    chunk[fill++] = b;
    return true;
  };

  // 1. Sync byte.
  if (!emit(kSyncByte)) return false;

  // 2. Header: 4 bytes (tag LE, length LE).
  for (size_t i = 0; i < kHeaderSize; ++i) {
    if (!emit(header[i])) return false;
  }

  // 3. Data: data_length bytes.
  if (data && data_length > 0) {
    for (uint16_t i = 0; i < data_length; ++i) {
      if (!emit(data[i])) return false;
    }
  }

  // 4. Checksum: XOR of header + data.
  uint8_t checksum = compute_checksum(header, data, data_length);
  if (!emit(checksum)) return false;

  return flush();
}

uint8_t Protocol::compute_checksum(const uint8_t* header, const uint8_t* data,
                                   uint16_t data_length) {
  uint8_t cs = 0;
  for (size_t i = 0; i < kHeaderSize; ++i) {
    cs ^= header[i];
  }
  if (data && data_length > 0) {
    for (uint16_t i = 0; i < data_length; ++i) {
      cs ^= data[i];
    }
  }
  return cs;
}

void Protocol::reset_parser() {
  state_ = State::kWaitingForSync;
  rx_index_ = 0;
  rx_data_length_ = 0;
  rx_tag_ = 0;
}

void Protocol::dispatch_frame() {
  // Search the handler table for a matching tag.
  for (uint8_t i = 0; i < handler_count_; ++i) {
    if (static_cast<uint16_t>(handlers_[i].tag) == rx_tag_) {
      handlers_[i].handler(handlers_[i].context, rx_buffer_ + kHeaderSize,
                           rx_data_length_);
      return;
    }
  }

  // No handler registered for this tag — frame is silently dropped.
}

void Protocol::consume_byte(uint8_t raw, uint8_t& out) { out = raw; }

void Protocol::loopback_impl_(void* context, const uint8_t* data,
                              uint16_t length) {
  auto* self = static_cast<Protocol*>(context);
  if (self->tx_data_) {
    return;  // TX busy, drop this loopback frame.
  }
  self->tx_tag_ = static_cast<uint16_t>(Tag::kLoopback);
  self->tx_length_ = length;
  if (length > 0) {
    std::memcpy(self->tx_data_buffer_, data, length);
  }
  self->tx_data_ = self->tx_data_buffer_;
}

}  // namespace protocol
