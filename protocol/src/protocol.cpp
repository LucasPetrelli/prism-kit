#include "protocol.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace protocol {

// ============================================================================
// Frame
// ============================================================================

Frame::Frame(Tag tag, const uint8_t* data, uint16_t length)
    : tag_(tag), data_(data), length_(length) {}

Frame Frame::Create(Tag tag, const uint8_t* data, uint16_t length) {
  if (length > kMaxFrameDataLength) {
    return Frame{};
  }
  if (length > 0 && !data) {
    return Frame{};
  }
  return Frame{tag, data, length};
}

FrameHeader Frame::ParseHeader(const uint8_t* wire_data) {
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
    : read_(config.read),
      write_(config.write),
      debug_(config.debug),
      timestamp_(config.timestamp),
      frame_timeout_(config.frame_timeout),
      verbose_(config.verbose) {
  // Tag 0x0000 (kLoopback) is always registered as the first handler entry.
  handlers_[static_cast<uint8_t>(Tag::kLoopback)] = {
    Tag::kLoopback, &Protocol::LoopbackImpl, this};
  handler_count_ = 1;
}

// ============================================================================
// Protocol — handler registration
// ============================================================================

bool Protocol::AddHandler(Tag tag, FrameHandler handler) {
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

bool Protocol::Send(const Frame& frame) {
  if (!frame.IsValid()) {
    return false;
  }
  if (tx_data_) {
    return false;
  }

  tx_tag_ = static_cast<uint16_t>(frame.GetTag());
  tx_length_ = frame.Length();
  if (frame.Data() && tx_length_ > 0) {
    // Copy into internal buffer so the caller can free/reuse their data.
    std::memcpy(tx_data_buffer_, frame.Data(), tx_length_);
  }
  tx_data_ = tx_data_buffer_;
  return true;
}

// ============================================================================
// Protocol — run (state machine)
// ============================================================================

void Protocol::Run() {
  // Attempt transmission of any pending frame before processing new
  // incoming data (gives a failed write from a previous Run() a chance
  // to retry) and again afterwards (to immediately send any frames
  // queued by a handler during dispatch).
  TxPhase();

  // ── RX phase: parse incoming bytes ──

  // Check frame timeout before processing bytes.
  if (frame_timeout_ > 0 && timestamp_ && state_ != State::kWaitingForSync) {
    uint32_t now = timestamp_();
    if (now - frame_start_ts_ > frame_timeout_) {
      DebugLog("Frame timeout (%u ticks)", frame_timeout_);
      ResetParser();
    }
  }

  uint8_t raw;
  while (ReadRawByte(raw)) {
    switch (state_) {
      // ---------------------------------------------------------------
      case State::kWaitingForSync:
        if (raw == kSyncByte) {
          ResetParser();
          if (timestamp_) {
            frame_start_ts_ = timestamp_();
          }
          state_ = State::kReadingHeader;
          DebugVerbose("SYNC detected");
        }
        // Discard everything else while searching for sync.
        break;

      // ---------------------------------------------------------------
      case State::kReadingHeader: {
        uint8_t byte;
        ConsumeByte(raw, byte);

        rx_buffer_[rx_index_] = byte;
        ++rx_index_;

        if (rx_index_ >= kHeaderSize) {
          FrameHeader hdr = Frame::ParseHeader(rx_buffer_);
          if (hdr.length > kMaxFrameDataLength) {
            DebugLog("Bad frame length: %u (max %u)", hdr.length,
                     kMaxFrameDataLength);
            ResetParser();
            break;
          }
          DebugVerbose("Header: tag=0x%04X len=%u", hdr.tag, hdr.length);
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
        ConsumeByte(raw, byte);

        rx_buffer_[kHeaderSize + rx_index_] = byte;
        ++rx_index_;

        if (rx_index_ >= rx_data_length_) {
          DebugVerbose("Data complete: %u bytes", rx_data_length_);
          state_ = State::kReadingChecksum;
        }
        break;
      }

      // ---------------------------------------------------------------
      case State::kReadingChecksum: {
        uint8_t received_checksum;
        ConsumeByte(raw, received_checksum);

        uint8_t expected = ComputeChecksum(rx_buffer_, rx_buffer_ + kHeaderSize,
                                           rx_data_length_);

        if (received_checksum == expected) {
          DebugVerbose("CRC OK");
          DispatchFrame();
          ResetParser();
        } else {
          DebugLog("CRC mismatch: got 0x%02X expected 0x%02X",
                   received_checksum, expected);
          ResetParser();
        }
        break;
      }
    }
  }

  // Transmit any frame that was queued by a handler during dispatch.
  TxPhase();
}

// ============================================================================
// Protocol — internal helpers
// ============================================================================

bool Protocol::ReadRawByte(uint8_t& out) {
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
      DebugVerbose(readahead_, readahead_cnt_, "Rx %u bytes:", readahead_cnt_);
      readahead_idx_ = 1;
      out = readahead_[0];
      return true;
    }
  }
  return false;
}

bool Protocol::WriteFrameWire(const uint8_t* header, const uint8_t* data,
                              uint16_t data_length) {
  if (!write_) {
    return false;
  }

  // Assemble the escaped frame in chunks so the transport receives at most
  // a handful of write_() calls instead of one per byte.
  constexpr size_t k_chunk_size = 64;
  uint8_t chunk[k_chunk_size];
  size_t fill = 0;

  // Flush the current chunk to the transport.  Returns true on success.
  auto flush = [&]() -> bool {
    if (fill == 0) {
      return true;
    }
    bool ok = write_(chunk, static_cast<uint32_t>(fill));
    fill = 0;
    return ok;
  };

  // Emit one raw byte into the chunk.
  auto emit = [&](uint8_t b) -> bool {
    if (fill + 1 > k_chunk_size) {
      if (!flush()) {
        return false;
      }
    }
    chunk[fill++] = b;
    return true;
  };

  // 1. Sync byte.
  if (!emit(kSyncByte)) {
    return false;
  }

  // 2. Header: 4 bytes (tag LE, length LE).
  for (size_t i = 0; i < kHeaderSize; ++i) {
    if (!emit(header[i])) {
      return false;
    }
  }

  // 3. Data: data_length bytes.
  if (data && data_length > 0) {
    for (uint16_t i = 0; i < data_length; ++i) {
      if (!emit(data[i])) {
        return false;
      }
    }
  }

  // 4. Checksum: XOR of header + data.
  const uint8_t checksum = ComputeChecksum(header, data, data_length);
  if (!emit(checksum)) {
    return false;
  }

  return flush();
}

uint8_t Protocol::ComputeChecksum(const uint8_t* header, const uint8_t* data,
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

void Protocol::ResetParser() {
  state_ = State::kWaitingForSync;
  rx_index_ = 0;
  rx_data_length_ = 0;
  rx_tag_ = 0;
  frame_start_ts_ = 0;
}

void Protocol::DispatchFrame() {
  // Search the handler table for a matching tag.
  for (uint8_t i = 0; i < handler_count_; ++i) {
    if (static_cast<uint16_t>(handlers_[i].tag) == rx_tag_) {
      DebugVerbose("Dispatch: tag=0x%04X", rx_tag_);
      handlers_[i].handler(handlers_[i].context, rx_buffer_ + kHeaderSize,
                           rx_data_length_);
      return;
    }
  }

  // No handler registered for this tag — frame is silently dropped.
}

void Protocol::ConsumeByte(uint8_t raw, uint8_t& out) { out = raw; }

void Protocol::TxPhase() {
  if (tx_data_) {
    uint8_t header[4];
    header[0] = static_cast<uint8_t>(tx_tag_ & 0xFF);
    header[1] = static_cast<uint8_t>((tx_tag_ >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>(tx_length_ & 0xFF);
    header[3] = static_cast<uint8_t>((tx_length_ >> 8) & 0xFF);

    if (WriteFrameWire(header, tx_data_, tx_length_)) {
      tx_data_ = nullptr;
    }
    // If write fails, retry on the next call.
  }
}

void Protocol::LoopbackImpl(void* context, const uint8_t* data,
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

// ============================================================================
// Protocol — debug_log
// ============================================================================

// Helper macro to forward variadic arguments to debug_log_impl.
// This avoids duplicating the va_list boilerplate in every overload.
#define PROTOCOL_DEBUG_LOG_IMPL(data, length, fmt) \
  do {                                             \
    std::va_list args;                             \
    va_start(args, fmt);                           \
    DebugLogImpl(data, length, fmt, args);         \
    va_end(args);                                  \
  } while (0)

void Protocol::DebugLog(const char* fmt, ...) const {
  PROTOCOL_DEBUG_LOG_IMPL(nullptr, 0, fmt);
}

void Protocol::DebugLog(const uint8_t* data, uint32_t length, const char* fmt,
                        ...) const {
  PROTOCOL_DEBUG_LOG_IMPL(data, length, fmt);
}

void Protocol::DebugVerbose(const char* fmt, ...) const {
  if (!verbose_) {
    return;
  }
  PROTOCOL_DEBUG_LOG_IMPL(nullptr, 0, fmt);
}

void Protocol::DebugVerbose(const uint8_t* data, uint32_t length,
                            const char* fmt, ...) const {
  if (!verbose_) {
    return;
  }
  PROTOCOL_DEBUG_LOG_IMPL(data, length, fmt);
}

#undef PROTOCOL_DEBUG_LOG_IMPL

void Protocol::DebugLogImpl(const uint8_t* data, uint32_t length,
                            const char* fmt, std::va_list args) const {
  if (!debug_) {
    return;
  }

  // Format the header: "[PROTO] <message>"
  char buf[128];
  const int header_len = std::vsnprintf(buf, sizeof(buf), fmt, args);
  if (header_len < 0) {
    return;
  }

  debug_("[PROTO] %s\n", buf);

  if (!data || length == 0U) {
    return;
  }

  // Hexdump: up to 16 bytes per line.
  char line[64];
  constexpr uint32_t k_line_capacity = sizeof(line) - 1U;
  for (uint32_t offset = 0U; offset < length; offset += 16U) {
    uint32_t pos = 0U;
    const uint32_t remain = length - offset;
    const uint32_t chunk = remain < 16U ? remain : 16U;
    for (uint32_t i = 0U; i < chunk; ++i) {
      if (i > 0U && pos < k_line_capacity) {
        line[pos++] = ' ';
      }
      if (pos >= k_line_capacity) {
        break;  // Line full — truncate this row.
      }
      const int wrote = std::snprintf(&line[pos], k_line_capacity - pos + 1U,
                                      "%02X", data[offset + i]);
      pos += static_cast<uint32_t>(wrote > 0 ? wrote : 0);
      pos = std::min(pos, k_line_capacity);
    }
    line[pos] = '\0';
    debug_("%s\n", line);
  }
}

}  // namespace protocol
