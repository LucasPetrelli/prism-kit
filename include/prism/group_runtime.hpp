#ifndef PRISM_GROUP_RUNTIME_HPP_
#define PRISM_GROUP_RUNTIME_HPP_

#include <cstdint>

#include "prism/group_record.hpp"
#include "prism/instruction_types.hpp"

namespace prism {

/// @brief Fixed runtime storage reserved for one group activation.
///
/// The controller resets this record for each pass while retaining its
/// immutable group metadata.
struct GroupRuntime {
  /// @brief Immutable group metadata copied at validation time.
  GroupRecord record{};
  /// @brief Next instruction position within record.
  std::uint32_t cursor{0U};
  /// @brief Local timestamp origin for the current pass.
  std::uint32_t start_time{0U};
  /// @brief Additional passes remaining after the current pass.
  LoopCount repeats_remaining{0U};
  /// @brief Number of timed instructions still outstanding.
  std::uint32_t active_count{0U};
  /// @brief Number of child activations still outstanding.
  std::uint32_t child_count{0U};
  /// @brief Whether this pass observed a positive scheduling boundary.
  bool positive_boundary_observed{false};
  /// @brief Parent runtime index, or kMaxGroups for the root.
  std::uint32_t parent_group_index{kMaxGroups};
  /// @brief Nesting depth used to advance children before their parents.
  std::uint8_t depth{0U};
  /// @brief Whether this runtime currently represents an active invocation.
  bool active{false};

  /// @brief Clear transient activation state while retaining group metadata.
  /// @return None.
  void Reset();
};

}  // namespace prism

#endif  // PRISM_GROUP_RUNTIME_HPP_
