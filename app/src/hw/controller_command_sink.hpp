#ifndef APP_HW_CONTROLLER_COMMAND_SINK_HPP_
#define APP_HW_CONTROLLER_COMMAND_SINK_HPP_

#include <cstdint>

#include "hw/controller_command.hpp"
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
/// 1. Register(protocol) — called from HwTask::Setup(). Registers all
///    controller-command handlers and reports any table or duplicate-tag
///    failure. The mailbox pointer may still be null at this point.
/// 2. SetMailbox(mailbox) — called from AppTask::Setup() after the APP
///    thread's EventMailbox is ready.  Enables the outbound path.
class ControllerCommandSink {
 public:
  /// @brief Access the process-wide singleton.
  /// @return Reference to the ControllerCommandSink singleton.
  static ControllerCommandSink& Instance();

  ControllerCommandSink(const ControllerCommandSink&) = delete;
  ControllerCommandSink& operator=(const ControllerCommandSink&) = delete;

  /// @brief Register all legacy and grouped controller-command handlers.
  /// @param protocol Protocol instance to register handlers with.
  /// @pre Called once during HwTask::Setup().
  /// @return True only when every handler is registered.
  bool Register(protocol::Protocol& protocol);

  /// @brief Set the mailbox that parsed commands are forwarded into.
  /// @param mailbox Non-owning pointer to an EventMailbox owned by AppTask.
  ///     May be null to disable forwarding (handlers silently drop frames).
  void SetMailbox(ControllerCommandMailbox* mailbox);

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

  /// @brief Handler for the SetSingleColorHsv tag.
  static void HandleSetSingleColorHsv(void* context, const uint8_t* data,
                                      uint16_t length);

  /// @brief Handler for the SetMultipleColorHsv tag.
  static void HandleSetMultipleColorHsv(void* context, const uint8_t* data,
                                        uint16_t length);

  /// @brief Queue one decoded command and report mailbox failure via protocol.
  /// @param context Protocol instance supplied by AddHandler.
  /// @param message Fully decoded command to copy into the mailbox.
  /// @param command_name Short command name for diagnostics.
  static void QueueCommand(void* context,
                           const ControllerCommandMessage& message,
                           const char* command_name);

  /// @brief Validate an exact-length new command payload before decoding.
  /// @param context Protocol instance supplied by AddHandler.
  /// @param data Payload pointer.
  /// @param length Payload length in bytes.
  /// @param expected_length Required payload length.
  /// @param command_name Short command name for diagnostics.
  /// @return True only when the pointer is non-null and the size is exact.
  static bool HasExactPayload(void* context, const uint8_t* data,
                              uint16_t length, uint16_t expected_length,
                              const char* command_name);

  /// @brief Handler for grouped RGB range instructions.
  static void HandleSetMultipleColorGrouped(void* context, const uint8_t* data,
                                            uint16_t length);

  /// @brief Handler for grouped RGB single-pixel instructions.
  static void HandleSetSingleColorGrouped(void* context, const uint8_t* data,
                                          uint16_t length);

  /// @brief Handler for grouped HSV single-pixel instructions.
  static void HandleSetSingleColorHsvGrouped(void* context, const uint8_t* data,
                                             uint16_t length);

  /// @brief Handler for grouped HSV range instructions.
  static void HandleSetMultipleColorHsvGrouped(void* context,
                                               const uint8_t* data,
                                               uint16_t length);

  /// @brief Handler for a child-group activation instruction.
  static void HandleRunGroup(void* context, const uint8_t* data,
                             uint16_t length);

  /// @brief Handler for a root Start repeat policy.
  static void HandleStart(void* context, const uint8_t* data, uint16_t length);

  /// @brief Non-owning pointer to the EventMailbox owned by AppTask.
  ///     Null until SetMailbox() is called; handlers silently drop frames
  ///     while null.
  ControllerCommandMailbox* mailbox_ = nullptr;
};

}  // namespace app::hw

#endif /* APP_HW_CONTROLLER_COMMAND_SINK_HPP_ */
