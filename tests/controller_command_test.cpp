#include "hw/controller_command.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "controller_command_dispatch.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hw/controller_command_sink.hpp"
#include "mock_strip.hpp"
#include "oshal/critical_section.hpp"
#include "oshal/event.hpp"
#include "oshal/mailbox.hpp"
#include "oshal/status.h"
#include "prism/controller.hpp"
#include "protocol.hpp"
#include "tags.hpp"

namespace {

constexpr std::size_t kMaxMessageSize = 64U;
constexpr std::size_t kMaxQueueCapacity = 20U;
constexpr std::size_t kMaxFakeMailboxes = 16U;
constexpr std::size_t kMaxFakeEvents = 16U;

struct FakeMailbox {
  void* backend{nullptr};
  std::size_t message_size{0U};
  std::size_t capacity{0U};
  std::size_t count{0U};
  std::size_t head{0U};
  std::array<std::uint8_t, kMaxMessageSize * kMaxQueueCapacity> messages{};
};

struct FakeEvent {
  void* backend{nullptr};
  std::uint32_t events{0U};
};

std::array<FakeMailbox, kMaxFakeMailboxes> g_fake_mailboxes{};
std::array<FakeEvent, kMaxFakeEvents> g_fake_events{};
std::vector<std::uint8_t> g_received_wire;
std::size_t g_received_offset{0U};
std::string g_protocol_debug;
std::uint32_t g_fake_time{0U};

FakeMailbox* FindMailbox(void* backend) {
  const auto found = std::find_if(
    g_fake_mailboxes.begin(), g_fake_mailboxes.end(),
    [backend](const FakeMailbox& entry) { return entry.backend == backend; });
  return (found == g_fake_mailboxes.end()) ? nullptr : &*found;
}

FakeEvent* FindEvent(void* backend) {
  const auto found = std::find_if(
    g_fake_events.begin(), g_fake_events.end(),
    [backend](const FakeEvent& entry) { return entry.backend == backend; });
  return (found == g_fake_events.end()) ? nullptr : &*found;
}

std::uint32_t ReadWire(std::uint8_t* buffer, std::uint32_t length) {
  const std::size_t available = g_received_wire.size() - g_received_offset;
  const std::size_t count =
    std::min(available, static_cast<std::size_t>(length));
  if (count == 0U) {
    return 0U;
  }
  std::memcpy(buffer, g_received_wire.data() + g_received_offset, count);
  g_received_offset += count;
  return static_cast<std::uint32_t>(count);
}

bool WriteWire(const std::uint8_t* data, std::uint32_t length) {
  static_cast<void>(data);
  static_cast<void>(length);
  return true;
}

int CaptureProtocolDebug(const char* format, ...) {
  std::array<char, 192U> buffer{};
  std::va_list args;
  va_start(args, format);
  const int result = std::vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  if (result >= 0) {
    g_protocol_debug = buffer.data();
  }
  return result;
}

std::uint32_t FakeTimestamp() { return g_fake_time; }

void IgnoreSchedule(std::uint32_t delay_ms) { static_cast<void>(delay_ms); }

void IgnoreFrame(void* context, const std::uint8_t* data,
                 std::uint16_t length) {
  static_cast<void>(context);
  static_cast<void>(data);
  static_cast<void>(length);
}

std::vector<std::uint8_t> MakeWireFrame(protocol::Tag tag,
                                        const std::vector<std::uint8_t>& data) {
  const auto raw_tag = static_cast<std::uint16_t>(tag);
  const auto length = static_cast<std::uint16_t>(data.size());
  std::vector<std::uint8_t> frame{
    protocol::kSyncByte,
    static_cast<std::uint8_t>(raw_tag & 0xFFU),
    static_cast<std::uint8_t>(raw_tag >> 8U),
    static_cast<std::uint8_t>(length & 0xFFU),
    static_cast<std::uint8_t>(length >> 8U),
  };
  frame.insert(frame.end(), data.begin(), data.end());

  std::uint8_t checksum = 0U;
  for (std::size_t i = 1U; i < frame.size(); ++i) {
    checksum ^= frame[i];
  }
  frame.push_back(checksum);
  return frame;
}

void AppendLe16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

protocol::ProtocolConfig TestProtocolConfig() {
  return {ReadWire, WriteWire, CaptureProtocolDebug};
}

}  // namespace

namespace oshal::internal {

void CriticalSectionEnter(void* backend) { static_cast<void>(backend); }

void CriticalSectionExit(void* backend) { static_cast<void>(backend); }

void EventInit(void* backend) {
  auto found = FindEvent(backend);
  if (found == nullptr) {
    const auto unused = std::find_if(
      g_fake_events.begin(), g_fake_events.end(),
      [](const FakeEvent& entry) { return entry.backend == nullptr; });
    if (unused != g_fake_events.end()) {
      found = &*unused;
    }
  }
  if (found != nullptr) {
    *found = {backend, 0U};
  }
}

void EventPost(void* backend, std::uint32_t events) {
  if (auto* found = FindEvent(backend); found != nullptr) {
    found->events |= events;
  }
}

void EventSet(void* backend, std::uint32_t events) {
  if (auto* found = FindEvent(backend); found != nullptr) {
    found->events = events;
  }
}

std::uint32_t EventClear(void* backend, std::uint32_t events) {
  if (auto* found = FindEvent(backend); found != nullptr) {
    const std::uint32_t previous = found->events;
    found->events &= ~events;
    return previous;
  }
  return 0U;
}

std::uint32_t EventWaitAny(void* backend, std::uint32_t events,
                           std::uint32_t timeout_ms) {
  static_cast<void>(timeout_ms);
  if (auto* found = FindEvent(backend); found != nullptr) {
    const std::uint32_t matched = found->events & events;
    found->events &= ~matched;
    return matched;
  }
  return 0U;
}

std::uint32_t EventWaitAll(void* backend, std::uint32_t events,
                           std::uint32_t timeout_ms) {
  return EventWaitAny(backend, events, timeout_ms);
}

std::uint32_t EventPoll(void* backend, std::uint32_t events) {
  if (auto* found = FindEvent(backend); found != nullptr) {
    return found->events & events;
  }
  return 0U;
}

int MailboxInit(void* backend, std::size_t message_size, std::size_t capacity) {
  if (message_size > kMaxMessageSize || capacity > kMaxQueueCapacity) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }
  auto found = FindMailbox(backend);
  if (found == nullptr) {
    const auto unused = std::find_if(
      g_fake_mailboxes.begin(), g_fake_mailboxes.end(),
      [](const FakeMailbox& entry) { return entry.backend == nullptr; });
    if (unused != g_fake_mailboxes.end()) {
      found = &*unused;
    }
  }
  if (found == nullptr) {
    return STATUS_ERR_BACKEND;
  }
  *found = {backend, message_size, capacity, 0U, 0U, {}};
  return STATUS_OK;
}

int MailboxSend(void* backend, const void* message) {
  auto* found = FindMailbox(backend);
  if (found == nullptr || message == nullptr) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }
  if (found->count >= found->capacity) {
    return STATUS_ERR_BACKEND;
  }
  const std::size_t index = (found->head + found->count) % found->capacity;
  std::memcpy(found->messages.data() + (index * found->message_size), message,
              found->message_size);
  ++found->count;
  return STATUS_OK;
}

bool MailboxReceive(void* backend, void* output) {
  auto* found = FindMailbox(backend);
  if (found == nullptr || output == nullptr || found->count == 0U) {
    return false;
  }
  std::memcpy(output,
              found->messages.data() + (found->head * found->message_size),
              found->message_size);
  found->head = (found->head + 1U) % found->capacity;
  --found->count;
  return true;
}

}  // namespace oshal::internal

namespace {

class ControllerCommandProtocolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_received_wire.clear();
    g_received_offset = 0U;
    g_protocol_debug.clear();
    sink_ = &app::hw::ControllerCommandSink::Instance();
    sink_->SetMailbox(&mailbox_);
    ASSERT_TRUE(sink_->Register(protocol_));
  }

  void TearDown() override { sink_->SetMailbox(nullptr); }

  void Send(protocol::Tag tag, const std::vector<std::uint8_t>& payload) {
    auto frame = MakeWireFrame(tag, payload);
    g_received_wire.insert(g_received_wire.end(), frame.begin(), frame.end());
    protocol_.Run();
  }

  app::hw::ControllerCommandMessage Receive() {
    app::hw::ControllerCommandMessage message{};
    EXPECT_TRUE(mailbox_.Receive(&message));
    return message;
  }

  oshal::EventFlagGroup event_group_;
  app::hw::ControllerCommandMailbox mailbox_{event_group_, 0x100U};
  protocol::Protocol protocol_{TestProtocolConfig()};
  app::hw::ControllerCommandSink* sink_{nullptr};
};

TEST_F(ControllerCommandProtocolTest, RegistersTwelveCommandsWithLoopback) {
  EXPECT_EQ(protocol::kMaxHandlers, 13U);
  EXPECT_FALSE(
    protocol_.AddHandler(static_cast<protocol::Tag>(0x0200U), &IgnoreFrame));
  EXPECT_FALSE(sink_->Register(protocol_));
}

TEST_F(ControllerCommandProtocolTest, RegistrationFailureIsReturned) {
  protocol::Protocol constrained{TestProtocolConfig()};
  ASSERT_TRUE(
    constrained.AddHandler(static_cast<protocol::Tag>(0x0200U), &IgnoreFrame));
  ASSERT_TRUE(
    constrained.AddHandler(static_cast<protocol::Tag>(0x0201U), &IgnoreFrame));

  EXPECT_FALSE(sink_->Register(constrained));
  EXPECT_FALSE(
    constrained.AddHandler(static_cast<protocol::Tag>(0x0202U), &IgnoreFrame));
}

TEST_F(ControllerCommandProtocolTest, RemovedDelayTagRemainsUnassigned) {
  protocol::Protocol protocol_without_commands{TestProtocolConfig()};
  EXPECT_TRUE(protocol_without_commands.AddHandler(
    static_cast<protocol::Tag>(0x0104U), &IgnoreFrame));
}

TEST_F(ControllerCommandProtocolTest, DecodesGroupedRgbRangeLittleEndian) {
  const std::vector<std::uint8_t> payload{
    0x34U, 0x12U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x12U, 0x34U, 0x56U, 0x78U,
  };
  Send(protocol::Tag::kSetMultipleColorGrouped, payload);

  const auto message = Receive();
  EXPECT_EQ(message.cmd, app::hw::ControllerCommand::kSetMultipleColorGrouped);
  EXPECT_EQ(message.grouped_set_multiple.mark, 0x1234U);
  EXPECT_EQ(message.grouped_set_multiple.r, 0x11U);
  EXPECT_EQ(message.grouped_set_multiple.g, 0x22U);
  EXPECT_EQ(message.grouped_set_multiple.b, 0x33U);
  EXPECT_EQ(message.grouped_set_multiple.range.start, 0x44U);
  EXPECT_EQ(message.grouped_set_multiple.range.end, 0x55U);
  EXPECT_EQ(message.grouped_set_multiple.group_id, 0x78563412U);
}

TEST_F(ControllerCommandProtocolTest, DecodesGroupedRgbSingleLittleEndian) {
  const std::vector<std::uint8_t> payload{
    0xCDU, 0xABU, 0x10U, 0x20U, 0x30U, 0x40U, 0xEFU, 0xCDU, 0xABU, 0x90U,
  };
  Send(protocol::Tag::kSetSingleColorGrouped, payload);

  const auto message = Receive();
  EXPECT_EQ(message.cmd, app::hw::ControllerCommand::kSetSingleColorGrouped);
  EXPECT_EQ(message.grouped_set_single.mark, 0xABCDU);
  EXPECT_EQ(message.grouped_set_single.r, 0x10U);
  EXPECT_EQ(message.grouped_set_single.g, 0x20U);
  EXPECT_EQ(message.grouped_set_single.b, 0x30U);
  EXPECT_EQ(message.grouped_set_single.index, 0x40U);
  EXPECT_EQ(message.grouped_set_single.group_id, 0x90ABCDEFU);
}

TEST_F(ControllerCommandProtocolTest, DecodesGroupedHsvSingleLittleEndian) {
  const std::vector<std::uint8_t> payload{
    0x78U, 0x56U, 0x9AU, 0xBCU, 0xDEU, 0xF0U, 0x78U, 0x56U, 0x34U, 0x12U,
  };
  Send(protocol::Tag::kSetSingleColorHsvGrouped, payload);

  const auto message = Receive();
  EXPECT_EQ(message.cmd, app::hw::ControllerCommand::kSetSingleColorHsvGrouped);
  EXPECT_EQ(message.grouped_set_single_hsv.mark, 0x5678U);
  EXPECT_EQ(message.grouped_set_single_hsv.h, 0x9AU);
  EXPECT_EQ(message.grouped_set_single_hsv.s, 0xBCU);
  EXPECT_EQ(message.grouped_set_single_hsv.v, 0xDEU);
  EXPECT_EQ(message.grouped_set_single_hsv.index, 0xF0U);
  EXPECT_EQ(message.grouped_set_single_hsv.group_id, 0x12345678U);
}

TEST_F(ControllerCommandProtocolTest, DecodesGroupedHsvRangeLittleEndian) {
  const std::vector<std::uint8_t> payload{
    0x02U, 0x01U, 0xA1U, 0xB2U, 0xC3U, 0xD4U, 0xE5U, 0x89U, 0x67U, 0x45U, 0x23U,
  };
  Send(protocol::Tag::kSetMultipleColorHsvGrouped, payload);

  const auto message = Receive();
  EXPECT_EQ(message.cmd,
            app::hw::ControllerCommand::kSetMultipleColorHsvGrouped);
  EXPECT_EQ(message.grouped_set_multiple_hsv.mark, 0x0102U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.h, 0xA1U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.s, 0xB2U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.v, 0xC3U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.range.start, 0xD4U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.range.end, 0xE5U);
  EXPECT_EQ(message.grouped_set_multiple_hsv.group_id, 0x23456789U);
}

TEST_F(ControllerCommandProtocolTest, DecodesRunGroupAndStartFields) {
  std::vector<std::uint8_t> run_group;
  AppendLe32(run_group, 0x12345678U);
  AppendLe16(run_group, 0xABCDU);
  AppendLe32(run_group, 0x90ABCDEFU);
  AppendLe16(run_group, 0x7654U);
  Send(protocol::Tag::kRunGroup, run_group);

  const auto run_message = Receive();
  EXPECT_EQ(run_message.cmd, app::hw::ControllerCommand::kRunGroup);
  EXPECT_EQ(run_message.run_group.parent_group_id, 0x12345678U);
  EXPECT_EQ(run_message.run_group.mark, 0xABCDU);
  EXPECT_EQ(run_message.run_group.target_group_id, 0x90ABCDEFU);
  EXPECT_EQ(run_message.run_group.additional_repeats, 0x7654U);

  Send(protocol::Tag::kStart, {0xFFU, 0xFFU});
  const auto start_message = Receive();
  EXPECT_EQ(start_message.cmd, app::hw::ControllerCommand::kStart);
  EXPECT_EQ(start_message.start.root_additional_repeats,
            prism::kForeverLoopCount);
}

TEST_F(ControllerCommandProtocolTest, RejectsShortAndOverlongNewPayloads) {
  struct PayloadSize {
    protocol::Tag tag;
    std::uint16_t length;
  };
  const std::array<PayloadSize, 6U> sizes{{
    {protocol::Tag::kSetMultipleColorGrouped, 11U},
    {protocol::Tag::kSetSingleColorGrouped, 10U},
    {protocol::Tag::kSetSingleColorHsvGrouped, 10U},
    {protocol::Tag::kSetMultipleColorHsvGrouped, 11U},
    {protocol::Tag::kRunGroup, 12U},
    {protocol::Tag::kStart, 2U},
  }};

  for (const auto& size : sizes) {
    for (const std::uint16_t length :
         {static_cast<std::uint16_t>(size.length - 1U),
          static_cast<std::uint16_t>(size.length + 1U)}) {
      Send(size.tag, std::vector<std::uint8_t>(length, 0x5AU));
      app::hw::ControllerCommandMessage message{};
      EXPECT_FALSE(mailbox_.Receive(&message));
    }
  }
}

TEST_F(ControllerCommandProtocolTest,
       PreservesLegacyMinimumLengthsAndTrailingByteAcceptance) {
  struct LegacyCommand {
    protocol::Tag tag;
    std::uint16_t minimum_length;
    app::hw::ControllerCommand command;
  };
  const std::array<LegacyCommand, 4U> legacy{{
    {protocol::Tag::kSetMultipleColor, 7U,
     app::hw::ControllerCommand::kSetMultipleColor},
    {protocol::Tag::kSetSingleColor, 6U,
     app::hw::ControllerCommand::kSetSingleColor},
    {protocol::Tag::kSetSingleColorHsv, 6U,
     app::hw::ControllerCommand::kSetSingleColorHsv},
    {protocol::Tag::kSetMultipleColorHsv, 7U,
     app::hw::ControllerCommand::kSetMultipleColorHsv},
  }};

  for (const auto& legacy_command : legacy) {
    Send(legacy_command.tag,
         std::vector<std::uint8_t>(legacy_command.minimum_length - 1U, 0U));
    app::hw::ControllerCommandMessage message{};
    EXPECT_FALSE(mailbox_.Receive(&message));

    Send(legacy_command.tag,
         std::vector<std::uint8_t>(legacy_command.minimum_length + 2U, 0x5AU));
    message = Receive();
    EXPECT_EQ(message.cmd, legacy_command.command);
  }
}

TEST_F(ControllerCommandProtocolTest, LegacyResetAndRunIgnorePayloadLength) {
  Send(protocol::Tag::kResetInstructions, {0x11U, 0x22U, 0x33U});
  EXPECT_EQ(Receive().cmd, app::hw::ControllerCommand::kResetInstructions);

  Send(protocol::Tag::kRun, {0x44U, 0x55U});
  EXPECT_EQ(Receive().cmd, app::hw::ControllerCommand::kRun);

  Send(protocol::Tag::kRun, {});
  EXPECT_EQ(Receive().cmd, app::hw::ControllerCommand::kRun);
}

TEST_F(ControllerCommandProtocolTest, MailboxCapacityFailureIsLoggedAndAtomic) {
  for (std::size_t i = 0U; i < kMaxQueueCapacity; ++i) {
    Send(protocol::Tag::kStart, {0U, 0U});
  }
  EXPECT_TRUE(g_protocol_debug.empty());

  Send(protocol::Tag::kStart, {0U, 0U});
  EXPECT_NE(g_protocol_debug.find("mailbox send failed"), std::string::npos);

  std::size_t received = 0U;
  while (true) {
    app::hw::ControllerCommandMessage message{};
    if (!mailbox_.Receive(&message)) {
      break;
    }
    EXPECT_EQ(message.cmd, app::hw::ControllerCommand::kStart);
    ++received;
  }
  EXPECT_EQ(received, kMaxQueueCapacity);
}

class ControllerCommandDispatchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_fake_time = 0U;
    controller_.SetStrip(&strip_);
    controller_.SetTimestampCallback(FakeTimestamp);
    controller_.SetScheduleCallback(IgnoreSchedule);
  }

  prism::test::MockStrip strip_;
  prism::Controller controller_;
};

TEST_F(ControllerCommandDispatchTest,
       LegacyRunAndNewStartUseSuccessGatedImmediateTick) {
  app::hw::ControllerCommandMessage add{};
  add.cmd = app::hw::ControllerCommand::kSetSingleColor;
  add.set_single = {0U, 0x11U, 0x22U, 0x33U, 0U};
  auto result = app::DispatchControllerCommand(controller_, add);
  ASSERT_TRUE(result.mutation_attempted);
  ASSERT_EQ(result.mutation_status, prism::ControllerStatus::kSuccess);

  const auto expected = prism::color::RgbColor{0x11U, 0x22U, 0x33U};
  EXPECT_CALL(*strip_.MutableLed(0U), SetColor(expected))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(strip_, Show()).WillOnce(testing::Return(0));

  app::hw::ControllerCommandMessage run{};
  run.cmd = app::hw::ControllerCommand::kRun;
  result = app::DispatchControllerCommand(controller_, run);
  EXPECT_TRUE(result.start_attempted);
  EXPECT_EQ(result.start_status, prism::ControllerStatus::kSuccess);
  EXPECT_TRUE(result.initial_run_attempted);
  EXPECT_EQ(result.initial_run_status, prism::ControllerStatus::kSuccess);
}

TEST_F(ControllerCommandDispatchTest,
       TimerProgressRunsLaterMarksWithoutImplicitStart) {
  app::hw::ControllerCommandMessage add{};
  add.cmd = app::hw::ControllerCommand::kSetSingleColor;
  add.set_single = {10U, 0xAAU, 0xBBU, 0xCCU, 1U};
  ASSERT_EQ(app::DispatchControllerCommand(controller_, add).mutation_status,
            prism::ControllerStatus::kSuccess);

  app::hw::ControllerCommandMessage start{};
  start.cmd = app::hw::ControllerCommand::kStart;
  start.start.root_additional_repeats = 0U;
  const auto start_result = app::DispatchControllerCommand(controller_, start);
  ASSERT_EQ(start_result.start_status, prism::ControllerStatus::kSuccess);
  ASSERT_EQ(start_result.initial_run_status, prism::ControllerStatus::kSuccess);

  g_fake_time = 10U;
  const auto expected = prism::color::RgbColor{0xAAU, 0xBBU, 0xCCU};
  EXPECT_CALL(*strip_.MutableLed(1U), SetColor(expected))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(strip_, Show()).WillOnce(testing::Return(0));
  EXPECT_EQ(app::RunControllerProgress(controller_),
            prism::ControllerStatus::kSuccess);
}

TEST_F(ControllerCommandDispatchTest,
       RunGroupCommandRoutesGroupedInstructionToTargetGroup) {
  app::hw::ControllerCommandMessage grouped{};
  grouped.cmd = app::hw::ControllerCommand::kSetSingleColorGrouped;
  grouped.grouped_set_single = {0U, 1U, 2U, 3U, 0U, 0x12345678U};
  auto result = app::DispatchControllerCommand(controller_, grouped);
  ASSERT_EQ(result.mutation_status, prism::ControllerStatus::kSuccess);

  app::hw::ControllerCommandMessage nested_run_group{};
  nested_run_group.cmd = app::hw::ControllerCommand::kRunGroup;
  nested_run_group.run_group = {1U, 0U, 0x12345678U, 0U};
  result = app::DispatchControllerCommand(controller_, nested_run_group);
  ASSERT_EQ(result.mutation_status, prism::ControllerStatus::kSuccess);

  app::hw::ControllerCommandMessage root_run_group{};
  root_run_group.cmd = app::hw::ControllerCommand::kRunGroup;
  root_run_group.run_group = {0U, 0U, 1U, 0U};
  result = app::DispatchControllerCommand(controller_, root_run_group);
  ASSERT_EQ(result.mutation_status, prism::ControllerStatus::kSuccess);

  const auto expected = prism::color::RgbColor{1U, 2U, 3U};
  EXPECT_CALL(*strip_.MutableLed(0U), SetColor(expected))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(strip_, Show()).WillOnce(testing::Return(0));

  app::hw::ControllerCommandMessage start{};
  start.cmd = app::hw::ControllerCommand::kStart;
  start.start.root_additional_repeats = 0U;
  result = app::DispatchControllerCommand(controller_, start);
  EXPECT_EQ(result.start_status, prism::ControllerStatus::kSuccess);
  EXPECT_EQ(result.initial_run_status, prism::ControllerStatus::kSuccess);
}

TEST_F(ControllerCommandDispatchTest, FailedStartDoesNotRun) {
  app::hw::ControllerCommandMessage run_group{};
  run_group.cmd = app::hw::ControllerCommand::kRunGroup;
  run_group.run_group = {0U, 0U, 0xDEADBEEFU, 0U};
  ASSERT_EQ(
    app::DispatchControllerCommand(controller_, run_group).mutation_status,
    prism::ControllerStatus::kSuccess);

  app::hw::ControllerCommandMessage start{};
  start.cmd = app::hw::ControllerCommand::kStart;
  start.start.root_additional_repeats = 0U;
  const auto result = app::DispatchControllerCommand(controller_, start);
  EXPECT_TRUE(result.start_attempted);
  EXPECT_EQ(result.start_status, prism::ControllerStatus::kInvalidProgram);
  EXPECT_FALSE(result.initial_run_attempted);
}

TEST_F(ControllerCommandDispatchTest,
       MutationCapacityAndBusyStatusesAreReturned) {
  app::hw::ControllerCommandMessage add{};
  add.cmd = app::hw::ControllerCommand::kSetSingleColor;
  for (std::uint8_t i = 0U; i < prism::kMaxInstructions; ++i) {
    add.set_single = {10U, 0U, 0U, 0U, i};
    ASSERT_EQ(app::DispatchControllerCommand(controller_, add).mutation_status,
              prism::ControllerStatus::kSuccess);
  }
  auto result = app::DispatchControllerCommand(controller_, add);
  EXPECT_TRUE(result.mutation_attempted);
  EXPECT_EQ(result.mutation_status,
            prism::ControllerStatus::kCapacityExhausted);

  app::hw::ControllerCommandMessage start{};
  start.cmd = app::hw::ControllerCommand::kStart;
  start.start.root_additional_repeats = 0U;
  result = app::DispatchControllerCommand(controller_, start);
  ASSERT_EQ(result.start_status, prism::ControllerStatus::kSuccess);
  ASSERT_TRUE(result.initial_run_attempted);

  result = app::DispatchControllerCommand(controller_, start);
  EXPECT_EQ(result.start_status, prism::ControllerStatus::kBusy);
  EXPECT_FALSE(result.initial_run_attempted);

  result = app::DispatchControllerCommand(controller_, add);
  EXPECT_EQ(result.mutation_status, prism::ControllerStatus::kBusy);
}

TEST_F(ControllerCommandDispatchTest, StartRepeatPolicyIsForwarded) {
  app::hw::ControllerCommandMessage add{};
  add.cmd = app::hw::ControllerCommand::kSetSingleColor;
  add.set_single = {5U, 0U, 0U, 0U, 0U};
  ASSERT_EQ(app::DispatchControllerCommand(controller_, add).mutation_status,
            prism::ControllerStatus::kSuccess);

  app::hw::ControllerCommandMessage start{};
  start.cmd = app::hw::ControllerCommand::kStart;
  start.start.root_additional_repeats = prism::kForeverLoopCount;
  const auto result = app::DispatchControllerCommand(controller_, start);
  EXPECT_EQ(result.start_status, prism::ControllerStatus::kSuccess);
  EXPECT_TRUE(result.initial_run_attempted);
}

}  // namespace
