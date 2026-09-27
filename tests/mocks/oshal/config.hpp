#ifndef PRISM_TEST_OSHAL_CONFIG_HPP_
#define PRISM_TEST_OSHAL_CONFIG_HPP_

#include <cstddef>

namespace oshal::config {

inline constexpr std::size_t kCriticalSectionStorageSize = 4U;
inline constexpr std::size_t kEventStorageSize = 32U;
inline constexpr std::size_t kMailboxStorageSize = 64U;
inline constexpr std::size_t kTimedEventStorageSize = 80U;

}  // namespace oshal::config

#endif  // PRISM_TEST_OSHAL_CONFIG_HPP_
