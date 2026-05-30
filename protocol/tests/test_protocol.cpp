#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "frame.hpp"
#include "gtest/gtest.h"
#include "protocol.hpp"
#include "tags.hpp"

namespace protocol {
namespace {

// ============================================================================
// Helpers — construct escaped wire-format bytes
// ============================================================================

/// Append a single byte to the vector, escaping if needed.
inline void push_escaped(std::vector<uint8_t>& buf, uint8_t b) {
  if (b == kSyncByte || b == kEscapeByte) {
    buf.push_back(kEscapeByte);
    buf.push_back(static_cast<uint8_t>(b ^ kEscapeXor));
  } else {
    buf.push_back(b);
  }
}

/// Build a complete frame on the wire:
///   sync | escaped tag(2 LE) | escaped length(2 LE) | escaped data | escaped
///   checksum
inline std::vector<uint8_t> make_wire(Tag tag, const uint8_t* data = nullptr,
                                      uint16_t length = 0) {
  std::vector<uint8_t> buf;
  buf.reserve(1 + kHeaderSize * 2 + length * 2 + 2);

  // Sync byte (never escaped).
  buf.push_back(kSyncByte);

  const uint16_t tag_val = static_cast<uint16_t>(tag);

  // Header: tag LE (2) + length LE (2), escaped.
  push_escaped(buf, static_cast<uint8_t>(tag_val & 0xFF));
  push_escaped(buf, static_cast<uint8_t>((tag_val >> 8) & 0xFF));
  push_escaped(buf, static_cast<uint8_t>(length & 0xFF));
  push_escaped(buf, static_cast<uint8_t>((length >> 8) & 0xFF));

  // Data, escaped.
  if (data && length > 0) {
    for (uint16_t i = 0; i < length; ++i) {
      push_escaped(buf, data[i]);
    }
  }

  // Checksum: XOR of unescaped header + data, then escaped.
  uint8_t cs = 0;
  cs ^= static_cast<uint8_t>(tag_val & 0xFF);
  cs ^= static_cast<uint8_t>((tag_val >> 8) & 0xFF);
  cs ^= static_cast<uint8_t>(length & 0xFF);
  cs ^= static_cast<uint8_t>((length >> 8) & 0xFF);
  if (data && length > 0) {
    for (uint16_t i = 0; i < length; ++i) {
      cs ^= data[i];
    }
  }
  push_escaped(buf, cs);

  return buf;
}

/// Build a wire frame with a deliberately wrong checksum.
inline std::vector<uint8_t> make_wire_bad_checksum(Tag tag, const uint8_t* data,
                                                   uint16_t length) {
  auto buf = make_wire(tag, data, length);
  // Corrupt the last byte (the checksum).
  if (!buf.empty()) {
    buf.back() ^= 0xFF;
  }
  return buf;
}

/// Build a wire frame with an invalid header length (> kMaxFrameDataLength).
inline std::vector<uint8_t> make_wire_bad_length(Tag tag) {
  std::vector<uint8_t> buf;
  buf.reserve(32);
  buf.push_back(kSyncByte);

  const uint16_t tag_val = static_cast<uint16_t>(tag);
  constexpr uint16_t bad_len = kMaxFrameDataLength + 1;

  push_escaped(buf, static_cast<uint8_t>(tag_val & 0xFF));
  push_escaped(buf, static_cast<uint8_t>((tag_val >> 8) & 0xFF));
  push_escaped(buf, static_cast<uint8_t>(bad_len & 0xFF));
  push_escaped(buf, static_cast<uint8_t>((bad_len >> 8) & 0xFF));

  // One byte of "data" plus checksum (invalid but enough to observe reset).
  push_escaped(buf, 0x00);

  uint8_t cs = 0;
  cs ^= static_cast<uint8_t>(tag_val & 0xFF);
  cs ^= static_cast<uint8_t>((tag_val >> 8) & 0xFF);
  cs ^= static_cast<uint8_t>(bad_len & 0xFF);
  cs ^= static_cast<uint8_t>((bad_len >> 8) & 0xFF);
  cs ^= 0x00;
  push_escaped(buf, cs);

  return buf;
}

// ============================================================================
// Test Fixture
// ============================================================================

class ProtocolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tx_data_.clear();
    reader_data_.clear();
    reader_offset_ = 0;
    handler_called_ = false;
    handler_data_.clear();
    stored_proto_ = nullptr;
  }

  // --- Write-capture callback ---
  static bool write_callback(const uint8_t* data, uint32_t length) {
    auto& self = *instance_;
    self.tx_data_.insert(self.tx_data_.end(), data, data + length);
    return true;
  }

  // --- Read callback — cursor-based source for StreamReader tests ---
  static uint32_t read_callback(uint8_t* buffer, uint32_t length) {
    auto& self = *instance_;
    uint32_t remaining =
      static_cast<uint32_t>(self.reader_data_.size()) - self.reader_offset_;
    uint32_t n = (length < remaining) ? length : remaining;
    if (n > 0) {
      std::memcpy(buffer, self.reader_data_.data() + self.reader_offset_, n);
      self.reader_offset_ += n;
    }
    return n;
  }

  // --- Handler callback ---
  // The Protocol API always passes the Protocol* as context.
  // Verify that contract and then use instance_ for test assertions.
  static void handler_callback(void* context, const uint8_t* data,
                               uint16_t length) {
    // The context pointer must match the Protocol we created in
    // make_protocol().  It is NOT the same as instance_ (ProtocolTest*)
    // because Protocol stores its own `this` in the handler table.
    EXPECT_EQ(context, stored_proto_);
    auto& self = *instance_;
    self.handler_called_ = true;
    self.handler_data_.assign(data, data + length);
  }

  /// Create a Protocol with write capture enabled.
  /// RX is handled via feed(); the stream reader is unused.
  /// The Protocol lives in proto_ so stored_proto_ remains valid.
  Protocol& make_protocol() {
    instance_ = this;
    ProtocolConfig cfg{};
    cfg.write = write_callback;
    cfg.read = nullptr;  // use feed() for RX
    proto_.emplace(cfg);
    stored_proto_ = &proto_.value();
    return proto_.value();
  }

  /// Create a Protocol that reads from the StreamReader callback instead
  /// of feed().  Call set_reader_data() first to stage the wire bytes.
  Protocol& make_protocol_with_reader() {
    instance_ = this;
    ProtocolConfig cfg{};
    cfg.write = write_callback;
    cfg.read = read_callback;
    proto_.emplace(cfg);
    stored_proto_ = &proto_.value();
    return proto_.value();
  }

  /// Create a TX-only protocol (no write callback, no read).
  Protocol make_tx_only() {
    instance_ = this;
    ProtocolConfig cfg{};
    cfg.read = nullptr;
    cfg.write = nullptr;
    return Protocol{cfg};
  }

  /// Stage wire bytes for the next StreamReader-based run().
  void set_reader_data(const std::vector<uint8_t>& wire) {
    reader_data_ = wire;
    reader_offset_ = 0;
  }

  /// Feed wire bytes and run the protocol.
  /// When expect_tx is true, calls run() twice so that frames queued
  /// by a handler (e.g. loopback echo) are transmitted on the second pass.
  void feed_and_run(Protocol& proto, const std::vector<uint8_t>& wire,
                    bool expect_tx = false) {
    proto.feed(wire.data(), static_cast<uint32_t>(wire.size()));
    proto.run();
    if (expect_tx) {
      proto.run();  // Flush TX queued by the dispatched handler.
    }
  }

  /// Verify the TX buffer contains exactly the expected wire bytes.
  void expect_tx_bytes(const std::vector<uint8_t>& expected) {
    ASSERT_EQ(tx_data_.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      EXPECT_EQ(tx_data_[i], expected[i]) << "Mismatch at byte " << i;
    }
  }

  std::optional<Protocol> proto_;

  static ProtocolTest* instance_;
  /// Stored Protocol pointer for verifying handler context.
  static Protocol* stored_proto_;

  // Captured TX output.
  std::vector<uint8_t> tx_data_;

  // StreamReader data source.
  std::vector<uint8_t> reader_data_;
  uint32_t reader_offset_{0};

  // Last handler invocation.
  bool handler_called_{false};
  std::vector<uint8_t> handler_data_;
};

ProtocolTest* ProtocolTest::instance_ = nullptr;
Protocol* ProtocolTest::stored_proto_ = nullptr;

// ============================================================================
// Constructor
// ============================================================================

TEST_F(ProtocolTest, Constructor_DefaultConfig) {
  auto& proto = make_protocol();
  // Protocol constructs without crashing; loopback handler is pre-registered.
  // Verify by sending a loopback frame.
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  auto wire = make_wire(Tag::kLoopback, payload, sizeof(payload));
  feed_and_run(proto, wire, /*expect_tx=*/true);

  // Loopback should have echoed the data back via write_callback.
  auto expected = make_wire(Tag::kLoopback, payload, sizeof(payload));
  expect_tx_bytes(expected);
}

TEST_F(ProtocolTest, Constructor_NullWrite) {
  instance_ = this;
  ProtocolConfig cfg{};
  cfg.read = nullptr;
  cfg.write = nullptr;
  Protocol proto{cfg};

  // send() queues the frame (it doesn't check for a write callback)...
  const uint8_t data[] = {0xAA};
  auto frame = Frame::create(Tag::kUserMin, data, sizeof(data));
  ASSERT_TRUE(frame.is_valid());
  EXPECT_TRUE(proto.send(frame));

  // ...but run() fails to transmit because write_ is null.
  proto.run();

  // TX should still be pending (retry on next run).
  // Another send() while busy should fail.
  EXPECT_FALSE(proto.send(frame));
}

TEST_F(ProtocolTest, Constructor_BothNull) {
  ProtocolConfig cfg{};
  cfg.read = nullptr;
  cfg.write = nullptr;
  Protocol proto{cfg};

  // Should not crash; loopback handler still registered.
  const uint8_t payload[] = {0x42};
  auto wire = make_wire(Tag::kLoopback, payload, sizeof(payload));
  proto.feed(wire.data(), static_cast<uint32_t>(wire.size()));
  proto.run();
  // Nothing to assert — just must not crash / hang.
}

// ============================================================================
// add_handler
// ============================================================================

TEST_F(ProtocolTest, AddHandler_Success) {
  auto& proto = make_protocol();

  constexpr Tag kTag = Tag::kUserMin;
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Send a frame with that tag; handler should be invoked.
  const uint8_t payload[] = {0xDE, 0xAD};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xDE);
  EXPECT_EQ(handler_data_[1], 0xAD);
}

TEST_F(ProtocolTest, AddHandler_DuplicateTag) {
  auto& proto = make_protocol();

  constexpr Tag kTag = Tag::kUserMin;
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));
  EXPECT_FALSE(proto.add_handler(kTag, handler_callback));
}

TEST_F(ProtocolTest, AddHandler_NullHandler) {
  auto& proto = make_protocol();
  EXPECT_FALSE(proto.add_handler(Tag::kUserMin, nullptr));
}

TEST_F(ProtocolTest, AddHandler_TableFull) {
  auto& proto = make_protocol();

  // kLoopback (index 0) is pre-registered, so 7 slots left.
  // Fill all 7 remaining slots.
  for (uint16_t i = 1; i < kMaxHandlers; ++i) {
    Tag t{static_cast<uint16_t>(static_cast<uint16_t>(Tag::kUserMin) + i)};
    EXPECT_TRUE(proto.add_handler(t, handler_callback)) << "Slot " << i;
  }

  // 9th registration should fail.
  Tag t{static_cast<uint16_t>(Tag::kUserMin) + kMaxHandlers};
  EXPECT_FALSE(proto.add_handler(t, handler_callback));
}

TEST_F(ProtocolTest, AddHandler_LoopbackAlreadyRegistered) {
  auto& proto = make_protocol();
  // kLoopback is pre-registered in the constructor.
  EXPECT_FALSE(proto.add_handler(Tag::kLoopback, handler_callback));
}

// ============================================================================
// send
// ============================================================================

TEST_F(ProtocolTest, Send_ValidFrame) {
  auto& proto = make_protocol();

  const uint8_t data[] = {0x01, 0x02, 0x03};
  auto frame = Frame::create(Tag::kUserMin, data, sizeof(data));
  ASSERT_TRUE(frame.is_valid());

  EXPECT_TRUE(proto.send(frame));

  // run() should transmit.
  proto.run();

  auto expected = make_wire(Tag::kUserMin, data, sizeof(data));
  expect_tx_bytes(expected);
}

TEST_F(ProtocolTest, Send_ZeroLengthFrame) {
  auto& proto = make_protocol();

  auto frame = Frame::create(Tag::kUserMin, nullptr, 0);
  ASSERT_TRUE(frame.is_valid());
  EXPECT_TRUE(proto.send(frame));

  proto.run();

  auto expected = make_wire(Tag::kUserMin, nullptr, 0);
  expect_tx_bytes(expected);
}

TEST_F(ProtocolTest, Send_InvalidFrame) {
  auto& proto = make_protocol();

  // Frame::create() sanitises bad inputs by returning a valid zero-length
  // frame, so send() will accept it and transmit an empty frame.
  auto frame = Frame::create(Tag::kUserMin, nullptr, 1);
  EXPECT_TRUE(frame.is_valid());  // sanitised → zero-length
  EXPECT_EQ(frame.length(), 0u);
  EXPECT_TRUE(proto.send(frame));

  // Similarly, a length exceeding kMaxFrameDataLength is sanitised.
  std::array<uint8_t, kMaxFrameDataLength + 1> big{};
  auto too_big =
    Frame::create(Tag::kUserMin, big.data(), static_cast<uint16_t>(big.size()));
  EXPECT_TRUE(too_big.is_valid());
  EXPECT_EQ(too_big.length(), 0u);
}

TEST_F(ProtocolTest, Send_DataCopied) {
  auto& proto = make_protocol();

  uint8_t data[] = {0x10, 0x20, 0x30};
  auto frame = Frame::create(Tag::kUserMin, data, sizeof(data));
  ASSERT_TRUE(frame.is_valid());
  EXPECT_TRUE(proto.send(frame));

  // Mutate the original data after send().
  data[0] = 0xFF;
  data[1] = 0xFF;
  data[2] = 0xFF;

  proto.run();

  // The TX should still contain the original bytes, not the mutated ones.
  const uint8_t original[] = {0x10, 0x20, 0x30};
  auto expected = make_wire(Tag::kUserMin, original, sizeof(original));
  expect_tx_bytes(expected);
}

TEST_F(ProtocolTest, Send_BusyTx) {
  auto& proto = make_protocol();

  const uint8_t data1[] = {0x01};
  auto frame1 = Frame::create(Tag::kUserMin, data1, sizeof(data1));
  ASSERT_TRUE(frame1.is_valid());
  EXPECT_TRUE(proto.send(frame1));

  // Second send before run() should fail (TX busy).
  const uint8_t data2[] = {0x02};
  auto frame2 = Frame::create(Tag::kUserMin, data2, sizeof(data2));
  EXPECT_FALSE(proto.send(frame2));

  // After run(), TX is free.
  proto.run();
  tx_data_.clear();

  EXPECT_TRUE(proto.send(frame2));
  proto.run();
  auto expected = make_wire(Tag::kUserMin, data2, sizeof(data2));
  expect_tx_bytes(expected);
}

// ============================================================================
// RX — wire format parsing
// ============================================================================

TEST_F(ProtocolTest, Rx_MinimalFrame) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0100};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Zero-length frame.
  auto wire = make_wire(kTag, nullptr, 0);
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  EXPECT_TRUE(handler_data_.empty());
}

TEST_F(ProtocolTest, Rx_FrameWithData) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0101};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  for (size_t i = 0; i < sizeof(payload); ++i) {
    EXPECT_EQ(handler_data_[i], payload[i]) << "Mismatch at index " << i;
  }
}

TEST_F(ProtocolTest, Rx_ChecksumMismatch) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0102};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0x11, 0x22};
  auto wire = make_wire_bad_checksum(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  // Handler should NOT be called because checksum was wrong.
  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Rx_EscapedSyncByte) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0103};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Payload contains kSyncByte (0xAA), which must be escaped.
  const uint8_t payload[] = {0xAA, 0x01, 0xAA, 0x02};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xAA);
  EXPECT_EQ(handler_data_[2], 0xAA);
}

TEST_F(ProtocolTest, Rx_EscapedEscapeByte) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0104};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Payload contains kEscapeByte (0xBB), which must be escaped.
  const uint8_t payload[] = {0xBB, 0x01, 0xBB, 0x02};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xBB);
  EXPECT_EQ(handler_data_[2], 0xBB);
}

TEST_F(ProtocolTest, Rx_UnexpectedSyncResync) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0105};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Build a frame but insert a raw sync byte in the middle of the data.
  const uint8_t payload_a[] = {0x11, 0x22};
  auto wire = make_wire(kTag, payload_a, sizeof(payload_a));
  // Replace an escaped byte in the data portion with a raw sync byte
  // to force a mid-frame resync. Find the data start (byte 5) and corrupt it.
  // Byte 5 (0-indexed) is the first data byte (sync=0, escaped header=4 bytes).
  // Actually we need to be more careful. Let's construct manually.
  //
  // Build: sync | header(4 escaped) | data_start | 0xAA(raw) | rest...
  // This will cause the parser to resync when it hits the raw 0xAA.

  std::vector<uint8_t> corrupt;
  corrupt.push_back(kSyncByte);  // sync

  const uint16_t tv = static_cast<uint16_t>(kTag);
  push_escaped(corrupt, static_cast<uint8_t>(tv & 0xFF));
  push_escaped(corrupt, static_cast<uint8_t>((tv >> 8) & 0xFF));

  const uint16_t len = 5;
  push_escaped(corrupt, static_cast<uint8_t>(len & 0xFF));
  push_escaped(corrupt, static_cast<uint8_t>((len >> 8) & 0xFF));

  // First data byte — OK.
  push_escaped(corrupt, 0x11);
  // Raw sync byte mid-data (unescaped!) — triggers resync.
  corrupt.push_back(kSyncByte);
  // Rest of the data.
  push_escaped(corrupt, 0x22);
  push_escaped(corrupt, 0x33);
  push_escaped(corrupt, 0x44);

  // Checksum (matching the original 5 bytes: 0x11, 0xAA, 0x22, 0x33, 0x44).
  uint8_t cs = 0;
  cs ^= static_cast<uint8_t>(tv & 0xFF);
  cs ^= static_cast<uint8_t>((tv >> 8) & 0xFF);
  cs ^= static_cast<uint8_t>(len & 0xFF);
  cs ^= static_cast<uint8_t>((len >> 8) & 0xFF);
  cs ^= 0x11;
  cs ^= 0xAA;
  cs ^= 0x22;
  cs ^= 0x33;
  cs ^= 0x44;
  push_escaped(corrupt, cs);

  feed_and_run(proto, corrupt);

  // Handler should NOT be called — the parser resyncs on the raw 0xAA
  // and the partial frame is discarded.
  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Rx_MultipleFrames) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0106};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t p1[] = {0xF1, 0xF2};
  const uint8_t p2[] = {0xB1, 0xB2, 0xB3};

  auto w1 = make_wire(kTag, p1, sizeof(p1));
  auto w2 = make_wire(kTag, p2, sizeof(p2));

  // Concatenate both frames into one feed.
  std::vector<uint8_t> combined;
  combined.insert(combined.end(), w1.begin(), w1.end());
  combined.insert(combined.end(), w2.begin(), w2.end());

  proto.feed(combined.data(), static_cast<uint32_t>(combined.size()));
  proto.run();

  // Handler called once for the first frame.
  // The second frame is also dispatched — but our handler only records the
  // last invocation.
  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(p2));
  EXPECT_EQ(handler_data_[0], 0xB1);
}

TEST_F(ProtocolTest, Rx_GarbageBeforeSync) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0107};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Prepend garbage bytes that are NOT kSyncByte.
  std::vector<uint8_t> wire;
  wire.insert(wire.end(), {0x00, 0x01, 0x02, 0xFF, 0xCC});

  const uint8_t payload[] = {0xCA, 0xFE};
  auto frame = make_wire(kTag, payload, sizeof(payload));
  wire.insert(wire.end(), frame.begin(), frame.end());

  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xCA);
  EXPECT_EQ(handler_data_[1], 0xFE);
}

TEST_F(ProtocolTest, Rx_LengthExceedsMax) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0108};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  auto wire = make_wire_bad_length(kTag);
  feed_and_run(proto, wire);

  // Handler should NOT be called — parser resets on bad length.
  EXPECT_FALSE(handler_called_);
}

// ============================================================================
// RX — StreamReader path (read callback instead of feed())
// ============================================================================

TEST_F(ProtocolTest, Rx_StreamReader_Basic) {
  auto& proto = make_protocol_with_reader();
  constexpr Tag kTag{0x0110};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0xA1, 0xB2, 0xC3};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);

  proto.run();  // reads via read_callback → dispatches frame

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xA1);
  EXPECT_EQ(handler_data_[1], 0xB2);
  EXPECT_EQ(handler_data_[2], 0xC3);
}

TEST_F(ProtocolTest, Rx_StreamReader_MultipleFrames) {
  auto& proto = make_protocol_with_reader();
  constexpr Tag kTag{0x0111};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t p1[] = {0x10, 0x20};
  const uint8_t p2[] = {0x30, 0x40, 0x50};
  auto w1 = make_wire(kTag, p1, sizeof(p1));
  auto w2 = make_wire(kTag, p2, sizeof(p2));

  std::vector<uint8_t> combined;
  combined.insert(combined.end(), w1.begin(), w1.end());
  combined.insert(combined.end(), w2.begin(), w2.end());
  set_reader_data(combined);

  proto.run();  // both frames dispatched in one run() pass

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(p2));
  EXPECT_EQ(handler_data_[0], 0x30);  // last frame's data
}

TEST_F(ProtocolTest, Rx_StreamReader_NoData) {
  auto& proto = make_protocol_with_reader();
  constexpr Tag kTag{0x0112};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Empty reader buffer — read_callback returns 0 immediately.
  set_reader_data({});
  proto.run();

  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Rx_StreamReader_LargeReadAhead) {
  auto& proto = make_protocol_with_reader();
  constexpr Tag kTag{0x0113};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Payload larger than the internal read-ahead buffer (16 bytes) to
  // verify that read_raw_byte() refills correctly across chunk boundaries.
  constexpr size_t kBig = 64;
  std::array<uint8_t, kBig> payload{};
  for (size_t i = 0; i < kBig; ++i) {
    payload[i] = static_cast<uint8_t>(i ^ 0x5A);
  }

  auto wire =
    make_wire(kTag, payload.data(), static_cast<uint16_t>(payload.size()));
  set_reader_data(wire);
  proto.run();

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(handler_data_[i], payload[i]) << "Mismatch at index " << i;
  }
}

// ============================================================================
// Loopback
// ============================================================================

TEST_F(ProtocolTest, Loopback_EchoesBack) {
  auto& proto = make_protocol();

  const uint8_t payload[] = {0xAB, 0xCD, 0xEF};
  auto wire = make_wire(Tag::kLoopback, payload, sizeof(payload));
  feed_and_run(proto, wire, /*expect_tx=*/true);

  // The loopback handler echoes the data back via the write callback.
  auto expected = make_wire(Tag::kLoopback, payload, sizeof(payload));
  expect_tx_bytes(expected);
}

TEST_F(ProtocolTest, Loopback_AfterUserFrame) {
  auto& proto = make_protocol();

  // Queue a user frame first.
  const uint8_t user_data[] = {0x77};
  auto user_frame = Frame::create(Tag::kUserMin, user_data, sizeof(user_data));
  ASSERT_TRUE(user_frame.is_valid());
  EXPECT_TRUE(proto.send(user_frame));

  // Feed a loopback frame and run.  TX phase sends the user frame first,
  // then RX dispatches the loopback (TX is now free), which queues an echo.
  const uint8_t loop_data[] = {0x88};
  auto loop_wire = make_wire(Tag::kLoopback, loop_data, sizeof(loop_data));
  proto.feed(loop_wire.data(), static_cast<uint32_t>(loop_wire.size()));
  proto.run();  // TX user frame + RX loopback → echo queued

  // First TX output: the user frame.
  auto expected_user = make_wire(Tag::kUserMin, user_data, sizeof(user_data));
  expect_tx_bytes(expected_user);

  // Second run() flushes the loopback echo.
  tx_data_.clear();
  proto.run();
  auto expected_loop = make_wire(Tag::kLoopback, loop_data, sizeof(loop_data));
  expect_tx_bytes(expected_loop);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(ProtocolTest, Edge_MaxPayloadLength) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0109};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Build 256-byte payload.
  std::array<uint8_t, kMaxFrameDataLength> payload{};
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>(i & 0xFF);
  }

  auto wire =
    make_wire(kTag, payload.data(), static_cast<uint16_t>(payload.size()));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(handler_data_[i], payload[i]) << "Mismatch at index " << i;
  }
}

TEST_F(ProtocolTest, Edge_NullFeed) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x010A};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // feed(nullptr, 0) should be a no-op.
  proto.feed(nullptr, 0);
  proto.run();
  EXPECT_FALSE(handler_called_);

  // feed(nullptr, non-zero) should also be safe.
  proto.feed(nullptr, 10);
  proto.run();
  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Edge_EmptyFeed) {
  auto& proto = make_protocol();

  // Feed with valid pointer but zero length.
  const uint8_t dummy = 0;
  proto.feed(&dummy, 0);
  proto.run();
  // Should not crash — nothing dispatched.
}

TEST_F(ProtocolTest, Edge_HeaderBytesNeedEscaping) {
  auto& proto = make_protocol();

  // Use a tag value that contains 0xAA or 0xBB to test header escaping.
  constexpr Tag kTag{0xAABB};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0x12, 0x34};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  feed_and_run(proto, wire);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0x12);
  EXPECT_EQ(handler_data_[1], 0x34);
}

}  // namespace
}  // namespace protocol
