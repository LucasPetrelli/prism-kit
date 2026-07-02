#ifndef APP_HW_CONTROLLER_COMMAND_SINK_HPP_
#define APP_HW_CONTROLLER_COMMAND_SINK_HPP_

#include <cstdint>

#include "hw/controller_command.hpp"
#include "oshal/event_mailbox.hpp"
#include "protocol.hpp"

namespace app::hw {

/// @brief Receives fully-parsed protocol frames for controller commands and
///     forwards them to the APP thread via an EventMailbox.
///
/// ControllerCommandSink is a process-wide singleton because its static
/// protocol adapter functions must reach instance state without a context
/// pointer (protocol::FrameHandler uses bare function pointers).
///
/// ## Two-phase initialisation
///
/// 1. Register(protocol) — called from HwTask::Setup().  Registers the four
///    static frame handlers on the given Protocol.  The mailbox pointer may
///    still be null at this point; handlers check and silently drop frames.
/// 2. SetMailbox(mailbox) — called from AppTask::Setup() after the APP
///    thread's EventMailbox is ready.  Enables the outbound path.
class ControllerCommandSink {
 public:
  /// @brief Access the process-wide singleton.
  /// @return Reference to the ControllerCommandSink singleton.
  static ControllerCommandSink& Instance();

  ControllerCommandSink(const ControllerCommandSink&) = delete;
  ControllerCommandSink& operator=(const ControllerCommandSink&) = delete;

  /// @brief Register the four controller-command frame handlers on a protocol
  ///     engine.
  /// @param protocol Protocol instance to register handlers with.
  /// @pre Called once during HwTask::Setup().
  void Register(protocol::Protocol& protocol);

  /// @brief Set the mailbox that parsed commands are forwarded into.
  /// @param mailbox Non-owning pointer to an EventMailbox owned by AppTask.
  ///     May be null to disable forwarding (handlers silently drop frames).
  void SetMailbox(
    oshal::EventMailbox<sizeof(ControllerCommandMessage), 4U>* mailbox);

  /// @brief Drain all pending commands from the mailbox and dispatch them
  ///     to the given animation controller.
  /// @param controller Controller to enqueue / reset / run instructions on.
  void DrainCommands(prism::Controller& controller);

 private:
  ControllerCommandSink() = default;

  /// @brief Handler for the SetMultipleColor tag.
  static void HandleSetMultipleColor(void* context, const uint8_t* data,
                                     uint16_t length);

  /// @brief Handler for the SetSingleColor tag.
  static void HandleSetSingleColor(void* context, const uint8_t* data,
                                   uint16_t length);

  /// @brief Handler for the ResetInstructions tag.
  static void HandleResetInstructions(void* context, const uint8_t* data,
                                      uint16_t length);

  /// @brief Handler for the Run tag.
  static void HandleRun(void* context, const uint8_t* data, uint16_t length);

  /// @brief Non-owning pointer to the EventMailbox owned by AppTask.
  ///     Null until SetMailbox() is called; handlers silently drop frames
  ///     while null.
  oshal::EventMailbox<sizeof(ControllerCommandMessage), 4U>* mailbox_ = nullptr;
};

}  // namespace app::hw

#endif /* APP_HW_CONTROLLER_COMMAND_SINK_HPP_ */
