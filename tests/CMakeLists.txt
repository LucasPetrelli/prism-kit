cmake_minimum_required(VERSION 3.20)
project(controller_test LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- GoogleTest + GMock via FetchContent ---
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Test executable ---
add_executable(controller_test
  controller_test.cpp
  controller_command_test.cpp
  hw_task_setup_test.cpp
  instruction_header_compile_test.cpp
  ../app/controller/controller.cpp
  ../app/controller/instructions.cpp
  ../app/src/controller_command_dispatch.cpp
  ../app/src/hw/command_manager.cpp
  ../app/src/hw/controller_command_sink.cpp
  ../app/src/hw/hw_task.cpp
  ../app/src/hw/status_led.cpp
  ../app/src/hw/strip_manager.cpp
  ../protocol/src/protocol.cpp
)

target_include_directories(controller_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/mocks
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../app/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../oshal/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../bal/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(controller_test PRIVATE cxx_std_17)
target_compile_definitions(controller_test PRIVATE PRISM_CONTROLLER_TESTING=1)

target_link_libraries(controller_test PRIVATE
  GTest::gmock_main
)

enable_testing()
include(GoogleTest)
gtest_discover_tests(controller_test)
