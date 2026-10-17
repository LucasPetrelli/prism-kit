#include "prism/group_runtime.hpp"

#include "prism/group_record.hpp"

void prism::GroupRuntime::Reset() {
  cursor = 0U;
  start_time = 0U;
  repeats_remaining = 0U;
  active_count = 0U;
  child_count = 0U;
  positive_boundary_observed = false;
  parent_group_index = prism::kMaxGroups;
  depth = 0U;
  active = false;
}
