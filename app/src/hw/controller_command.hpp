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
  /// @brief Set a range of pixels to one color in a specified group.
  kSetMultipleColorGrouped,
  /// @brief Set a single pixel to one color in a specified group.
  kSetSingleColorGrouped,
  /// @brief Set a range of pixels from HSV in a specified group.
  kSetMultipleColorHsvGrouped,
  /// @brief Set a single pixel from HSV in a specified group.
  kSetSingleColorHsvGrouped,
  /// @brief Add a RunGroup instruction to a parent group.
  kRunGroup,
  /// @brief Start the retained program with a root repeat policy.
  kStart,
};

/// @brief Grouped RGB range fields copied from the protocol payload.
struct GroupedSetMultipleColorPayload {
  prism::Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  prism::Range range;
  prism::GroupId group_id;
};

/// @brief Grouped RGB single-pixel fields copied from the protocol payload.
struct GroupedSetSingleColorPayload {
  prism::Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t index;
  prism::GroupId group_id;
};

/// @brief Grouped HSV single-pixel fields copied from the protocol payload.
struct GroupedSetSingleColorHsvPayload {
  prism::Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  std::uint8_t index;
  prism::GroupId group_id;
};

/// @brief Grouped HSV range fields copied from the protocol payload.
struct GroupedSetMultipleColorHsvPayload {
  prism::Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  prism::Range range;
  prism::GroupId group_id;
};

/// @brief RunGroup fields decoded from the packed wire sequence.
struct RunGroupCommandPayload {
  prism::GroupId parent_group_id;
  prism::Mark mark;
  prism::GroupId target_group_id;
  prism::LoopCount additional_repeats;
};

/// @brief Root repeat policy decoded from the Start payload.
struct StartCommandPayload {
  prism::LoopCount root_additional_repeats;
};

/// @brief IPC message sent from the HW thread (protocol handlers) to the APP
///     thread (AppTask) to drive the animation controller.
///
/// The sender decodes wire fields explicitly, then the receiver constructs
/// the concrete ControllerInstruction and applies it to the controller.
struct ControllerCommandMessage {
  /// @brief Which controller action to perform.
  ControllerCommand cmd;

  /// @brief Payload data for the command.
  ///
  /// The ``mark`` field inside instruction payloads is an offset in the
  /// owning group's local timeline. For reset and legacy run, it is unused.
  union {
    /// @brief Payload for a SetMultipleColor instruction.
    prism::SetMultipleColorPayload set_multiple;
    /// @brief Payload for a SetSingleColor instruction.
    prism::SetSingleColorPayload set_single;
    /// @brief Payload for a SetMultipleColorHsv instruction.
    prism::SetMultipleColorHsvPayload set_multiple_hsv;
    /// @brief Payload for a SetSingleColorHsv instruction.
    prism::SetSingleColorHsvPayload set_single_hsv;
    /// @brief Payload for a grouped SetMultipleColor instruction.
    GroupedSetMultipleColorPayload grouped_set_multiple;
    /// @brief Payload for a grouped SetSingleColor instruction.
    GroupedSetSingleColorPayload grouped_set_single;
    /// @brief Payload for a grouped SetMultipleColorHsv instruction.
    GroupedSetMultipleColorHsvPayload grouped_set_multiple_hsv;
    /// @brief Payload for a grouped SetSingleColorHsv instruction.
    GroupedSetSingleColorHsvPayload grouped_set_single_hsv;
    /// @brief Payload for a RunGroup instruction.
    RunGroupCommandPayload run_group;
    /// @brief Payload for an explicit Start command.
    StartCommandPayload start;
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
