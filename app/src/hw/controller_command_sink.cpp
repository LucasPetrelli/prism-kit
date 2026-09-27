#include "controller_command_sink.hpp"

#include <cstdint>
#include <cstring>

#include "hw/controller_command.hpp"
#include "oshal/status.h"
#include "prism/instruction.hpp"
#include "protocol.hpp"
#include "tags.hpp"

namespace {

std::uint16_t ReadLe16(const std::uint8_t* data) {
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t ReadLe32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

}  // namespace

namespace app::hw {

ControllerCommandSink& ControllerCommandSink::Instance() {
  static ControllerCommandSink instance;
  return instance;
}

bool ControllerCommandSink::Register(protocol::Protocol& protocol) {
  return protocol.AddHandler(protocol::Tag::kSetMultipleColor,
                             &HandleSetMultipleColor) &&
         protocol.AddHandler(protocol::Tag::kSetSingleColor,
                             &HandleSetSingleColor) &&
         protocol.AddHandler(protocol::Tag::kResetInstructions,
                             &HandleResetInstructions) &&
         protocol.AddHandler(protocol::Tag::kRun, &HandleRun) &&
         protocol.AddHandler(protocol::Tag::kSetSingleColorHsv,
                             &HandleSetSingleColorHsv) &&
         protocol.AddHandler(protocol::Tag::kSetMultipleColorHsv,
                             &HandleSetMultipleColorHsv) &&
         protocol.AddHandler(protocol::Tag::kSetMultipleColorGrouped,
                             &HandleSetMultipleColorGrouped) &&
         protocol.AddHandler(protocol::Tag::kSetSingleColorGrouped,
                             &HandleSetSingleColorGrouped) &&
         protocol.AddHandler(protocol::Tag::kSetSingleColorHsvGrouped,
                             &HandleSetSingleColorHsvGrouped) &&
         protocol.AddHandler(protocol::Tag::kSetMultipleColorHsvGrouped,
                             &HandleSetMultipleColorHsvGrouped) &&
         protocol.AddHandler(protocol::Tag::kRunGroup, &HandleRunGroup) &&
         protocol.AddHandler(protocol::Tag::kStart, &HandleStart);
}

void ControllerCommandSink::SetMailbox(ControllerCommandMailbox* mailbox) {
  mailbox_ = mailbox;
}

void ControllerCommandSink::QueueCommand(
  void* context, const ControllerCommandMessage& message,
  const char* command_name) {
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  const int status = self.mailbox_->Send(&message);
  if (status != STATUS_OK && context != nullptr) {
    static_cast<protocol::Protocol*>(context)->DebugLog(
      "Controller command %s mailbox send failed (%d)", command_name, status);
  }
}

bool ControllerCommandSink::HasExactPayload(void* context, const uint8_t* data,
                                            uint16_t length,
                                            uint16_t expected_length,
                                            const char* command_name) {
  if (data != nullptr && length == expected_length) {
    return true;
  }
  if (context != nullptr) {
    static_cast<protocol::Protocol*>(context)->DebugLog(
      "Dropped controller command %s: expected %u bytes, received %u",
      command_name, expected_length, length);
  }
  return false;
}

// ---------------------------------------------------------------------------
// Static handler implementations
// ---------------------------------------------------------------------------

void ControllerCommandSink::HandleSetMultipleColor(void* context,
                                                   const uint8_t* data,
                                                   uint16_t length) {
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  if (length < sizeof(prism::SetMultipleColorPayload)) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetMultipleColor;
  msg.set_multiple.mark = static_cast<prism::Mark>(data[0]) |
                          (static_cast<prism::Mark>(data[1]) << 8U);
  msg.set_multiple.r = data[2];
  msg.set_multiple.g = data[3];
  msg.set_multiple.b = data[4];
  msg.set_multiple.range.start = data[5];
  msg.set_multiple.range.end = data[6];

  QueueCommand(context, msg, "SetMultipleColor");
}

void ControllerCommandSink::HandleSetSingleColor(void* context,
                                                 const uint8_t* data,
                                                 uint16_t length) {
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  if (length < sizeof(prism::SetSingleColorPayload)) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetSingleColor;
  msg.set_single.mark = static_cast<prism::Mark>(data[0]) |
                        (static_cast<prism::Mark>(data[1]) << 8U);
  msg.set_single.r = data[2];
  msg.set_single.g = data[3];
  msg.set_single.b = data[4];
  msg.set_single.index = data[5];

  QueueCommand(context, msg, "SetSingleColor");
}

void ControllerCommandSink::HandleSetSingleColorHsv(void* context,
                                                    const uint8_t* data,
                                                    uint16_t length) {
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  if (length < sizeof(prism::SetSingleColorHsvPayload)) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetSingleColorHsv;
  msg.set_single_hsv.mark = static_cast<prism::Mark>(data[0]) |
                            (static_cast<prism::Mark>(data[1]) << 8U);
  msg.set_single_hsv.h = data[2];
  msg.set_single_hsv.s = data[3];
  msg.set_single_hsv.v = data[4];
  msg.set_single_hsv.index = data[5];

  QueueCommand(context, msg, "SetSingleColorHsv");
}

void ControllerCommandSink::HandleSetMultipleColorHsv(void* context,
                                                      const uint8_t* data,
                                                      uint16_t length) {
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  if (length < sizeof(prism::SetMultipleColorHsvPayload)) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetMultipleColorHsv;
  msg.set_multiple_hsv.mark = static_cast<prism::Mark>(data[0]) |
                              (static_cast<prism::Mark>(data[1]) << 8U);
  msg.set_multiple_hsv.h = data[2];
  msg.set_multiple_hsv.s = data[3];
  msg.set_multiple_hsv.v = data[4];
  msg.set_multiple_hsv.range.start = data[5];
  msg.set_multiple_hsv.range.end = data[6];

  QueueCommand(context, msg, "SetMultipleColorHsv");
}

void ControllerCommandSink::HandleResetInstructions(void* context,
                                                    const uint8_t* data,
                                                    uint16_t length) {
  static_cast<void>(data);
  static_cast<void>(length);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kResetInstructions;
  QueueCommand(context, msg, "ResetInstructions");
}

void ControllerCommandSink::HandleRun(void* context, const uint8_t* data,
                                      uint16_t length) {
  static_cast<void>(data);
  static_cast<void>(length);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kRun;
  QueueCommand(context, msg, "Run");
}

void ControllerCommandSink::HandleSetMultipleColorGrouped(void* context,
                                                          const uint8_t* data,
                                                          uint16_t length) {
  if (!HasExactPayload(context, data, length, 11U, "Grouped RGB range")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kSetMultipleColorGrouped;
  msg.grouped_set_multiple.mark = ReadLe16(data);
  msg.grouped_set_multiple.r = data[2];
  msg.grouped_set_multiple.g = data[3];
  msg.grouped_set_multiple.b = data[4];
  msg.grouped_set_multiple.range.start = data[5];
  msg.grouped_set_multiple.range.end = data[6];
  msg.grouped_set_multiple.group_id = ReadLe32(data + 7);
  QueueCommand(context, msg, "Grouped RGB range");
}

void ControllerCommandSink::HandleSetSingleColorGrouped(void* context,
                                                        const uint8_t* data,
                                                        uint16_t length) {
  if (!HasExactPayload(context, data, length, 10U, "Grouped RGB single")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kSetSingleColorGrouped;
  msg.grouped_set_single.mark = ReadLe16(data);
  msg.grouped_set_single.r = data[2];
  msg.grouped_set_single.g = data[3];
  msg.grouped_set_single.b = data[4];
  msg.grouped_set_single.index = data[5];
  msg.grouped_set_single.group_id = ReadLe32(data + 6);
  QueueCommand(context, msg, "Grouped RGB single");
}

void ControllerCommandSink::HandleSetSingleColorHsvGrouped(void* context,
                                                           const uint8_t* data,
                                                           uint16_t length) {
  if (!HasExactPayload(context, data, length, 10U, "Grouped HSV single")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kSetSingleColorHsvGrouped;
  msg.grouped_set_single_hsv.mark = ReadLe16(data);
  msg.grouped_set_single_hsv.h = data[2];
  msg.grouped_set_single_hsv.s = data[3];
  msg.grouped_set_single_hsv.v = data[4];
  msg.grouped_set_single_hsv.index = data[5];
  msg.grouped_set_single_hsv.group_id = ReadLe32(data + 6);
  QueueCommand(context, msg, "Grouped HSV single");
}

void ControllerCommandSink::HandleSetMultipleColorHsvGrouped(
  void* context, const uint8_t* data, uint16_t length) {
  if (!HasExactPayload(context, data, length, 11U, "Grouped HSV range")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kSetMultipleColorHsvGrouped;
  msg.grouped_set_multiple_hsv.mark = ReadLe16(data);
  msg.grouped_set_multiple_hsv.h = data[2];
  msg.grouped_set_multiple_hsv.s = data[3];
  msg.grouped_set_multiple_hsv.v = data[4];
  msg.grouped_set_multiple_hsv.range.start = data[5];
  msg.grouped_set_multiple_hsv.range.end = data[6];
  msg.grouped_set_multiple_hsv.group_id = ReadLe32(data + 7);
  QueueCommand(context, msg, "Grouped HSV range");
}

void ControllerCommandSink::HandleRunGroup(void* context, const uint8_t* data,
                                           uint16_t length) {
  if (!HasExactPayload(context, data, length, 12U, "RunGroup")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kRunGroup;
  msg.run_group.parent_group_id = ReadLe32(data);
  msg.run_group.mark = ReadLe16(data + 4);
  msg.run_group.target_group_id = ReadLe32(data + 6);
  msg.run_group.additional_repeats = ReadLe16(data + 10);
  QueueCommand(context, msg, "RunGroup");
}

void ControllerCommandSink::HandleStart(void* context, const uint8_t* data,
                                        uint16_t length) {
  if (!HasExactPayload(context, data, length, 2U, "Start")) {
    return;
  }

  ControllerCommandMessage msg{};
  msg.cmd = ControllerCommand::kStart;
  msg.start.root_additional_repeats = ReadLe16(data);
  QueueCommand(context, msg, "Start");
}

}  // namespace app::hw
