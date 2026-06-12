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
// Helpers — construct wire-format bytes
// ============================================================================

/// Build a complete frame on the wire:
///   sync | tag(2 LE) | length(2 LE) | data | checksum
inline std::vector<uint8_t> make_wire(Tag tag, const uint8_t* data = nullptr,
                                      uint16_t length = 0) {
  std::vector<uint8_t> buf;
  buf.reserve(1 + kHeaderSize + length + 1);

  // Sync byte.
  buf.push_back(kSyncByte);

  const uint16_t tag_val = static_cast<uint16_t>(tag);

  // Header: tag LE (2) + length LE (2).
  buf.push_back(static_cast<uint8_t>(tag_val & 0xFF));
  buf.push_back(static_cast<uint8_t>((tag_val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(length & 0xFF));
  buf.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));

  // Data.
  if (data && length > 0) {
    for (uint16_t i = 0; i < length; ++i) {
      buf.push_back(data[i]);
    }
  }

  // Checksum: XOR of header + data.
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
  buf.push_back(cs);

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

  buf.push_back(static_cast<uint8_t>(tag_val & 0xFF));
  buf.push_back(static_cast<uint8_t>((tag_val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(bad_len & 0xFF));
  buf.push_back(static_cast<uint8_t>((bad_len >> 8) & 0xFF));

  // One byte of "data" plus checksum (invalid but enough to observe reset).
  buf.push_back(0x00);

  uint8_t cs = 0;
  cs ^= static_cast<uint8_t>(tag_val & 0xFF);
  cs ^= static_cast<uint8_t>((tag_val >> 8) & 0xFF);
  cs ^= static_cast<uint8_t>(bad_len & 0xFF);
  cs ^= static_cast<uint8_t>((bad_len >> 8) & 0xFF);
  cs ^= 0x00;
  buf.push_back(cs);

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

  /// Create a Protocol with write capture and StreamReader-based RX.
  /// Call set_reader_data() first to stage the wire bytes, then run().
  Protocol& make_protocol() {
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

  /// Run the protocol through the StreamReader path and optionally
  /// flush a queued TX frame (e.g. loopback echo) on a second pass.
  void reader_run(Protocol& proto, bool flush_tx = false) {
    proto.run();
    if (flush_tx) {
      proto.run();
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

  // --- Timestamp / timeout support ---
  static uint32_t fake_timestamp_;

  /// @brief Timestamp callback that returns the injectable fake_timestamp_.
  static uint32_t timestamp_callback() { return fake_timestamp_; }

  /// Create a Protocol with StreamReader-based RX and timeout support.
  Protocol& make_protocol_with_timeout(uint32_t frame_timeout) {
    instance_ = this;
    fake_timestamp_ = 0;
    ProtocolConfig cfg{};
    cfg.write = write_callback;
    cfg.read = read_callback;
    cfg.timestamp = timestamp_callback;
    cfg.frame_timeout = frame_timeout;
    proto_.emplace(cfg);
    stored_proto_ = &proto_.value();
    return proto_.value();
  }

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
uint32_t ProtocolTest::fake_timestamp_ = 0;

// ============================================================================
// Constructor
// ============================================================================

TEST_F(ProtocolTest, Constructor_DefaultConfig) {
  auto& proto = make_protocol();
  // Protocol constructs without crashing; loopback handler is pre-registered.
  // Verify by sending a loopback frame.
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  auto wire = make_wire(Tag::kLoopback, payload, sizeof(payload));
  set_reader_data(wire);
  reader_run(proto, /*flush_tx=*/true);

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
  set_reader_data(wire);
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
  set_reader_data(wire);
  reader_run(proto);

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
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
  EXPECT_TRUE(handler_data_.empty());
}

TEST_F(ProtocolTest, Rx_FrameWithData) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0101};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);
  reader_run(proto);

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
  set_reader_data(wire);
  reader_run(proto);

  // Handler should NOT be called because checksum was wrong.
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

  set_reader_data(combined);
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

  set_reader_data(wire);
  reader_run(proto);

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
  set_reader_data(wire);
  reader_run(proto);

  // Handler should NOT be called — parser resets on bad length.
  EXPECT_FALSE(handler_called_);
}

// ============================================================================
// RX — StreamReader path (read callback instead of feed())
// ============================================================================

TEST_F(ProtocolTest, Rx_StreamReader_Basic) {
  auto& proto = make_protocol();
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
  auto& proto = make_protocol();
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
  auto& proto = make_protocol();
  constexpr Tag kTag{0x0112};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Empty reader buffer — read_callback returns 0 immediately.
  set_reader_data({});
  proto.run();

  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Rx_StreamReader_LargeReadAhead) {
  auto& proto = make_protocol();
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
  set_reader_data(wire);
  reader_run(proto, /*flush_tx=*/true);

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

  // Feed a loopback frame via StreamReader.  The first tx_phase() sends
  // the pending user frame, then RX dispatches the loopback (TX is now
  // free), which queues an echo that is immediately flushed by the
  // second tx_phase().
  const uint8_t loop_data[] = {0x88};
  auto loop_wire = make_wire(Tag::kLoopback, loop_data, sizeof(loop_data));
  set_reader_data(loop_wire);
  proto.run();  // tx → rx loopback → echo → tx (all in one call)

  // Both frames are transmitted in a single run() call.
  auto expected_user = make_wire(Tag::kUserMin, user_data, sizeof(user_data));
  auto expected_loop = make_wire(Tag::kLoopback, loop_data, sizeof(loop_data));
  std::vector<uint8_t> expected;
  expected.insert(expected.end(), expected_user.begin(), expected_user.end());
  expected.insert(expected.end(), expected_loop.begin(), expected_loop.end());
  expect_tx_bytes(expected);

  // Second run() has nothing to do — no pending TX, no reader data.
  tx_data_.clear();
  proto.run();
  EXPECT_TRUE(tx_data_.empty());
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
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    EXPECT_EQ(handler_data_[i], payload[i]) << "Mismatch at index " << i;
  }
}

TEST_F(ProtocolTest, Edge_NoReaderData) {
  auto& proto = make_protocol();
  constexpr Tag kTag{0x010A};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Empty reader buffer — read_callback returns 0.
  set_reader_data({});
  proto.run();
  EXPECT_FALSE(handler_called_);
}

TEST_F(ProtocolTest, Edge_ZeroLengthReaderData) {
  auto& proto = make_protocol();

  // Empty reader buffer — read_callback returns 0.
  set_reader_data({});
  proto.run();
  // Should not crash — nothing dispatched.
}

TEST_F(ProtocolTest, Edge_HeaderBytesWithSpecialValues) {
  auto& proto = make_protocol();

  // Use a tag value that contains 0xAA and 0xBB to verify round-trip
  // without any byte-stuffing.
  constexpr Tag kTag{0xAABB};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  const uint8_t payload[] = {0x12, 0x34};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0x12);
  EXPECT_EQ(handler_data_[1], 0x34);
}

// ============================================================================
// Frame timeout
// ============================================================================

TEST_F(ProtocolTest, Timeout_DisabledWhenZero) {
  // timeout = 0 means no timeout; frame completes normally.
  auto& proto = make_protocol_with_timeout(0);
  constexpr Tag kTag{0x0200};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Advance the clock far into the future before feeding.
  fake_timestamp_ = 10'000;

  const uint8_t payload[] = {0x42};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
}

TEST_F(ProtocolTest, Timeout_CompletesBeforeDeadline) {
  auto& proto = make_protocol_with_timeout(100);
  constexpr Tag kTag{0x0201};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Timestamp advances a little between sync and frame completion.
  fake_timestamp_ = 10;
  // Feed partial: sync byte + header (5 bytes) in one go.
  // This transitions to ReadingHeader → ReadingData.
  const uint8_t payload[] = {0x01, 0x02};
  auto wire = make_wire(kTag, payload, sizeof(payload));

  // Feed the whole frame; timestamps pick up on sync then after each byte.
  // The clock advances once at sync time; rest is within the same run() call
  // so the timeout check happens at the top of the next run().
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
}

TEST_F(ProtocolTest, Timeout_ResetsOnExpiry) {
  auto& proto = make_protocol_with_timeout(50);
  constexpr Tag kTag{0x0202};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Feed only the sync byte via reader — parser moves to kReadingHeader.
  std::vector<uint8_t> partial;
  partial.push_back(kSyncByte);
  set_reader_data(partial);
  fake_timestamp_ = 0;
  proto.run();  // sees sync, records frame_start_ts_ = 0, state → ReadingHeader

  // Advance clock past the timeout and call run() again.
  // Parser should reset to kWaitingForSync.
  fake_timestamp_ = 100;
  proto.run();

  // Now feed a complete frame — it should be parsed normally since the
  // stale partial frame was discarded.
  const uint8_t payload[] = {0xAB};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);
  reader_run(proto);

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xAB);
}

TEST_F(ProtocolTest, Timeout_ResetsOnlyWhenExpired) {
  auto& proto = make_protocol_with_timeout(100);
  constexpr Tag kTag{0x0203};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Feed partial frame (sync byte only) via reader.
  std::vector<uint8_t> partial;
  partial.push_back(kSyncByte);
  set_reader_data(partial);
  fake_timestamp_ = 0;
  proto.run();

  // Advance clock but stay within the timeout window.
  fake_timestamp_ = 50;
  proto.run();  // should NOT reset — still within timeout

  // Feed the rest of the frame via reader (skip sync byte already consumed).
  // Rebuild reader data with the remaining bytes.
  const uint8_t payload[] = {0xCD};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  reader_data_.assign(wire.data() + 1, wire.data() + wire.size());
  reader_offset_ = 0;
  fake_timestamp_ = 60;
  proto.run();

  EXPECT_TRUE(handler_called_);
  ASSERT_EQ(handler_data_.size(), sizeof(payload));
  EXPECT_EQ(handler_data_[0], 0xCD);
}

TEST_F(ProtocolTest, Timeout_NoTimestampCallbackDisablesTimeout) {
  // When timestamp callback is null, timeout value is ignored.
  instance_ = this;
  ProtocolConfig cfg{};
  cfg.write = write_callback;
  cfg.read = read_callback;
  cfg.timestamp = nullptr;  // no clock
  cfg.frame_timeout = 10;   // non-zero, but should be ignored
  Protocol proto{cfg};
  stored_proto_ = &proto;

  constexpr Tag kTag{0x0204};
  EXPECT_TRUE(proto.add_handler(kTag, handler_callback));

  // Feed a complete frame via reader — should be received normally.
  const uint8_t payload[] = {0xEF};
  auto wire = make_wire(kTag, payload, sizeof(payload));
  set_reader_data(wire);
  proto.run();

  EXPECT_TRUE(handler_called_);
}

}  // namespace
}  // namespace protocol
