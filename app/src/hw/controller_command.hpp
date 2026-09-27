#ifndef APP_HW_CONTROLLER_COMMAND_HPP_
#define APP_HW_CONTROLLER_COMMAND_HPP_

#include <cstdint>

#include "oshal/event_mailbox.hpp"
#include "prism/instruction.hpp"

namespace app::hw {

/// @brief Identifies which controller action an IPC command carries.
enum class ControllerCommand : std::uint8_t {
  /// @brief Set a range of pixels to one color (SetMultipleColor).
  kSetMultipleColor,
  /// @brief Set a single pixel to one color (SetSingleColor).
  kSetSingleColor,
  /// @brief Set a range of pixels from an HSV color (SetMultipleColorHsv).
  kSetMultipleColorHsv,
  /// @brief Set a single pixel from an HSV color (SetSingleColorHsv).
  kSetSingleColorHsv,
  /// @brief Clear all queued instructions (ResetInstructions).
  kResetInstructions,
  /// @brief Execute all queued instructions (Run).
  kRun,
};

/// @brief IPC message sent from the HW thread (protocol handlers) to the APP
///     thread (AppTask) to drive the animation controller.
///
/// The sender parses the wire-format payload into one of the existing
/// instruction payload structs, then the receiver constructs the concrete
/// ControllerInstruction subclass and enqueues it.
struct ControllerCommandMessage {
  /// @brief Which controller action to perform.
  ControllerCommand cmd;

  /// @brief Payload data for the command.
  ///
  /// The ``mark`` field inside each payload identifies the absolute ms
  /// timestamp at which the instruction should start executing.
  /// For kResetInstructions and kRun the payload is unused.
  union {
    /// @brief Payload for a SetMultipleColor instruction.
    prism::SetMultipleColorPayload set_multiple;
    /// @brief Payload for a SetSingleColor instruction.
    prism::SetSingleColorPayload set_single;
    /// @brief Payload for a SetMultipleColorHsv instruction.
    prism::SetMultipleColorHsvPayload set_multiple_hsv;
    /// @brief Payload for a SetSingleColorHsv instruction.
    prism::SetSingleColorHsvPayload set_single_hsv;
  };
};

/// @brief Mailbox type for delivering ControllerCommandMessage from the HW
///     thread to the APP thread.
/// @note Change the capacity here (second template argument) — it is the
///     single source of truth for all consumers.
using ControllerCommandMailbox =
  oshal::EventMailbox<sizeof(ControllerCommandMessage), 20>;

}  // namespace app::hw

#endif /* APP_HW_CONTROLLER_COMMAND_HPP_ */
