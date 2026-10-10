#ifndef PRISM_TEST_DEBUG_PORT_ACCESS_HPP_
#define PRISM_TEST_DEBUG_PORT_ACCESS_HPP_

#include <string>

namespace prism::test {

/// @brief Clear output captured by the host test's OSHAL debug-port fake.
void ClearDebugPortOutput();

/// @brief Copy the output captured by the host test's OSHAL debug-port fake.
/// @return Captured debug-port bytes as a string.
std::string DebugPortOutput();

}  // namespace prism::test

#endif /* PRISM_TEST_DEBUG_PORT_ACCESS_HPP_ */