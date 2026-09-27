#include "controller_command_sink.hpp"

#include <cstdint>
#include <cstring>

#include "hw/controller_command.hpp"
#include "prism/instruction.hpp"
#include "protocol.hpp"
#include "tags.hpp"

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
  protocol.AddHandler(protocol::Tag::kSetSingleColorHsv,
                      &HandleSetSingleColorHsv);
  protocol.AddHandler(protocol::Tag::kSetMultipleColorHsv,
                      &HandleSetMultipleColorHsv);
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

  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleSetSingleColorHsv(void* context,
                                                    const uint8_t* data,
                                                    uint16_t length) {
  static_cast<void>(context);
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

  self.mailbox_->Send(&msg);
}

void ControllerCommandSink::HandleSetMultipleColorHsv(void* context,
                                                      const uint8_t* data,
                                                      uint16_t length) {
  static_cast<void>(context);
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

}  // namespace app::hw
