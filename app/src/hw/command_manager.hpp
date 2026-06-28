#ifndef APP_HW_COMMAND_MANAGER_HPP_
#define APP_HW_COMMAND_MANAGER_HPP_

#include <cstdint>

#include "oshal/debug_port.hpp"
#include "oshal/event.hpp"
#include "oshal/serial_port.hpp"
#include "protocol.hpp"

namespace app::hw {

/// @brief Owns the command-port serial transport and its associated
///     protocol engine.
///
/// CommandManager is a process-wide singleton because its static protocol
/// adapter functions must reach instance state without a context pointer
/// (protocol::ProtocolConfig uses bare function pointers).
class CommandManager {
 public:
  /// @brief Event bitmask posted when UART data arrives on the command
  ///     port.
  static constexpr std::uint32_t kCommandRxEventMask = oshal::UserEvent(1);

  /// @brief Access the process-wide singleton.
  /// @return Reference to the CommandManager singleton.
  static CommandManager& Instance();

  CommandManager(const CommandManager&) = delete;
  CommandManager& operator=(const CommandManager&) = delete;

  /// @brief Wire the command port, debug port, and task wake-up event
  ///     group.
  /// @param command_port Optional OSHAL command port (may be null).
  /// @param debug_port   OSHAL debug port (must be ready, for protocol
  ///     debug output).
  /// @param event_group  EventFlagGroup the task sleeps on; the command
  ///     port's RX ISR will post kCommandRxEventMask to it.
  /// @pre debug_port and event_group have been validated by the caller.
  void Configure(oshal::SerialPort* command_port, oshal::DebugPort* debug_port,
                 oshal::EventFlagGroup* event_group);

  /// @brief Run one iteration of the protocol RX/TX state machine.
  ///
  /// Feeds any available RX bytes through the parser and retries pending
  /// TX frames.
  void Run();

  /// @brief Print the startup banner on the debug port.
  /// @param strip_name Name of the backend strip to include in the banner.
  /// @return false when the port write fails.
  bool PrintBanner(const char* strip_name);

  /// @brief Accessor for the stored command port pointer (may be null).
  /// @return Pointer to the command port, or nullptr.
  oshal::SerialPort* command_port() const { return command_port_; }

  /// @brief Event mask to wait on for command-port RX activity.
  /// @return kCommandRxEventMask.
  std::uint32_t rx_event_mask() const { return kCommandRxEventMask; }

  CommandManager() = default;

 private:
  /// @brief Protocol StreamReader adapter — reads from the command port.
  static std::uint32_t ReadAdapter(std::uint8_t* buffer, std::uint32_t length);

  /// @brief Protocol StreamWriter adapter — writes to the command port.
  static bool WriteAdapter(const std::uint8_t* data, std::uint32_t length);

  /// @brief Protocol DebugPrintf adapter — forwards to the debug port.
  static int DebugPrintfAdapter(const char* fmt, ...);

  oshal::SerialPort* command_port_ = nullptr;
  oshal::DebugPort* debug_port_ = nullptr;
  protocol::Protocol protocol_{
    protocol::ProtocolConfig{ReadAdapter, WriteAdapter, DebugPrintfAdapter}};
};

}  // namespace app::hw

#endif /* APP_HW_COMMAND_MANAGER_HPP_ */
