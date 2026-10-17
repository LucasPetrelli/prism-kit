#ifndef PRISM_INSTRUCTION_TYPES_HPP_
#define PRISM_INSTRUCTION_TYPES_HPP_

#include <cstdint>

namespace prism {

/// @brief Millisecond offset from the owning group's activation time.
///
/// The current wire commands retain their existing mark representation; a
/// group's local origin is applied by the controller.
using Mark = std::uint16_t;

/// @brief Label identifying an instruction group.
///
/// Group IDs are labels rather than indexes.  ID 0 is reserved for the
/// controller's implicit root group.
using GroupId = std::uint32_t;

/// @brief Additional passes requested for a group activation.
///
/// A value of zero means one pass.  The all-ones value requests forever for a
/// root start policy or a non-root RunGroup activation.
using LoopCount = std::uint16_t;

/// @brief Sentinel requesting unbounded root or child-group repetition.
constexpr LoopCount kForeverLoopCount = 0xFFFFU;

/// @brief Largest finite additional-repeat count.
constexpr LoopCount kMaxFiniteLoopCount = kForeverLoopCount - 1U;

/// @brief Shared fixed capacity for stored instructions and pending indexes.
constexpr std::uint32_t kMaxInstructions = 28U;

/// @brief Tag identifying the concrete instruction type.
enum class InstructionTag : std::uint8_t {
  /// @brief Range-fill instruction (SetMultipleColor).
  kSetMultipleColor,
  /// @brief Single-pixel instruction (SetSingleColor).
  kSetSingleColor,
  /// @brief HSV range-fill instruction (SetMultipleColorHsv).
  kSetMultipleColorHsv,
  /// @brief HSV single-pixel instruction (SetSingleColorHsv).
  kSetSingleColorHsv,
  /// @brief Start a non-root group timeline.
  kRunGroup,
  /// @brief Fade the HSV value of one staged pixel.
  kFade,
  /// @brief Interpolate one staged pixel to an RGB target in OKLab.
  kGradient,
#if defined(PRISM_CONTROLLER_TESTING)
  /// @brief Host-test-only timed instruction.
  kTestTimed,
#endif
};

/// @brief Return a human-readable name for an instruction tag.
/// @param tag Tag to stringify.
/// @return Pointer to a static string describing the instruction type.
const char* InstructionToString(InstructionTag tag);

/// @brief Half-open [start, end) pixel index range.
#pragma pack(push, 1)
struct Range {
  /// @brief Zero-based start index (inclusive).
  std::uint8_t start;
  /// @brief Zero-based end index (exclusive).
  std::uint8_t end;
};

/// @brief Wire-format payload for a SetMultipleColor instruction.
/// The ``mark`` field is an offset within the owning group's timeline.
struct SetMultipleColorPayload {
  Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  Range range;
};

/// @brief Wire-format payload for a SetSingleColor instruction.
struct SetSingleColorPayload {
  Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t index;
};

/// @brief Wire-format payload for a SetSingleColorHsv instruction.
struct SetSingleColorHsvPayload {
  Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  std::uint8_t index;
};

/// @brief Wire-format payload for a SetMultipleColorHsv instruction.
struct SetMultipleColorHsvPayload {
  Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  Range range;
};

/// @brief Wire-format payload for a one-pixel Fade instruction.
struct FadePayload {
  Mark mark;
  std::uint16_t percent;
  std::uint16_t duration_ms;
  std::uint8_t index;
  GroupId group_id;
};

/// @brief Fixed-width configuration for a one-pixel Gradient instruction.
struct GradientPayload {
  Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint16_t duration_ms;
  std::uint8_t index;
  GroupId group_id;
};
#pragma pack(pop)

/// @brief Controller-side configuration for a RunGroup instruction.
///
/// This is not a serialized protocol payload.  Wire tags and byte layouts are
/// defined by the protocol integration step.
struct RunGroupPayload {
  /// @brief Group whose local timeline schedules this activation.
  GroupId group_id{0U};
  /// @brief Offset within the parent group's local timeline.
  Mark mark{0U};
  /// @brief Nonzero sparse label of the group to activate.
  GroupId target_id{0U};
  /// @brief Additional child passes, or kForeverLoopCount.
  LoopCount additional_repeats{0U};
};

static_assert(sizeof(Range) == 2U, "Range must be 2 bytes (packed)");
static_assert(sizeof(SetMultipleColorPayload) == 7U,
              "SetMultipleColorPayload must be 7 bytes (mark + r,g,b,range)");
static_assert(sizeof(SetSingleColorPayload) == 6U,
              "SetSingleColorPayload must be 6 bytes (mark + r,g,b,index)");
static_assert(sizeof(SetSingleColorHsvPayload) == 6U,
              "SetSingleColorHsvPayload must be 6 bytes (mark + h,s,v,index)");
static_assert(
  sizeof(SetMultipleColorHsvPayload) == 7U,
  "SetMultipleColorHsvPayload must be 7 bytes (mark + h,s,v,range)");
static_assert(
  sizeof(FadePayload) == 11U,
  "FadePayload must be 11 bytes (mark, percent, duration, index, group)");
static_assert(
  sizeof(GradientPayload) == 12U,
  "GradientPayload must be 12 bytes (mark, rgb, duration, index, group)");

}  // namespace prism

#endif  // PRISM_INSTRUCTION_TYPES_HPP_
