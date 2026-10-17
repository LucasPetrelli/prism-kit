#ifndef PRISM_GROUP_RECORD_HPP_
#define PRISM_GROUP_RECORD_HPP_

#include <cstdint>

#include "prism/instruction_types.hpp"

namespace prism {

/// @brief Maximum number of group records, including the implicit root.
constexpr std::uint32_t kMaxGroups = kMaxInstructions + 1U;

/// @brief Immutable metadata describing one compact group view.
struct GroupRecord {
  /// @brief Sparse label identifying the group.
  GroupId id{0U};
  /// @brief Offset into the controller's shared instruction-index pool.
  std::uint32_t index_begin{0U};
  /// @brief Number of instruction indexes belonging to this group.
  std::uint32_t instruction_count{0U};
};

}  // namespace prism

#endif  // PRISM_GROUP_RECORD_HPP_
