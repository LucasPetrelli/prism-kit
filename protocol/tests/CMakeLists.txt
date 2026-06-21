cmake_minimum_required(VERSION 3.20)
project(protocol_test LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- GoogleTest via FetchContent ---
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# Prevent gtest from installing itself into the build tree.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Test executable ---
add_executable(protocol_test
  test_protocol.cpp
  ../src/protocol.cpp
)

target_include_directories(protocol_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(protocol_test PRIVATE cxx_std_17)

target_link_libraries(protocol_test PRIVATE
  GTest::gtest_main
)

# Suppress fetch-content developer warnings from external code.
enable_testing()
include(GoogleTest)
gtest_discover_tests(protocol_test)
