#include "controller_command_sink.hpp"

#include <cstring>

#include "hw/controller_command.hpp"
#include "protocol.hpp"

namespace app::hw {

ControllerCommandSink& ControllerCommandSink::Instance() {
  static ControllerCommandSink instance;
  return instance;
}

void ControllerCommandSink::Register(protocol::Protocol& protocol) {
  protocol.AddHandler(protocol::Tag::kSetMultipleColor,
                      &HandleSetMultipleColor);
  protocol.AddHandler(protocol::Tag::kSetSingleColor, &HandleSetSingleColor);
  protocol.AddHandler(protocol::Tag::kResetInstructions,
                      &HandleResetInstructions);
  protocol.AddHandler(protocol::Tag::kRun, &HandleRun);
  protocol.AddHandler(protocol::Tag::kDelay, &HandleDelay);
}

void ControllerCommandSink::SetMailbox(ControllerCommandMailbox* mailbox) {
  mailbox_ = mailbox;
}

// ---------------------------------------------------------------------------
// Static handler implementations
// ---------------------------------------------------------------------------

void ControllerCommandSink::HandleSetMultipleColor(void* context,
                                                   const uint8_t* data,
                                                   uint16_t length) {
  static_cast<void>(context);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  // Wire format: r (1), g (1), b (1), start (1), end (1) = 5 bytes.
  if (length < 5U) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetMultipleColor;
  msg.set_multiple.r = data[0];
  msg.set_multiple.g = data[1];
  msg.set_multiple.b = data[2];
  msg.set_multiple.range.start = data[3];
  msg.set_multiple.range.end = data[4];

  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleSetSingleColor(void* context,
                                                 const uint8_t* data,
                                                 uint16_t length) {
  static_cast<void>(context);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  // Wire format: r (1), g (1), b (1), index (1) = 4 bytes.
  if (length < 4U) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kSetSingleColor;
  msg.set_single.r = data[0];
  msg.set_single.g = data[1];
  msg.set_single.b = data[2];
  msg.set_single.index = data[3];

  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleResetInstructions(void* context,
                                                    const uint8_t* data,
                                                    uint16_t length) {
  static_cast<void>(context);
  static_cast<void>(data);
  static_cast<void>(length);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kResetInstructions;
  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleRun(void* context, const uint8_t* data,
                                      uint16_t length) {
  static_cast<void>(context);
  static_cast<void>(data);
  static_cast<void>(length);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kRun;
  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleDelay(void* context, const uint8_t* data,
                                        uint16_t length) {
  static_cast<void>(context);
  auto& self = Instance();
  if (self.mailbox_ == nullptr) {
    return;
  }

  // Wire format: delay_ms (4 bytes, uint32_t little-endian).
  if (length < 4U) {
    return;
  }

  ControllerCommandMessage msg;
  msg.cmd = ControllerCommand::kDelay;
  msg.delay_ms = static_cast<std::uint32_t>(data[0]) |
                 (static_cast<std::uint32_t>(data[1]) << 8U) |
                 (static_cast<std::uint32_t>(data[2]) << 16U) |
                 (static_cast<std::uint32_t>(data[3]) << 24U);
  self.mailbox_->Send(&msg);
}

}  // namespace app::hw
