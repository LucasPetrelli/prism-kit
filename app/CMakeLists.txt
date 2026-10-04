# APP contains product logic only. It depends on BAL and OSHAL headers but keeps
# Zephyr-specific APIs out of the public application interface.
set(PRISM_KIT_APP_BACKEND "HW" CACHE STRING "Prism Kit APP backend")
set_property(CACHE PRISM_KIT_APP_BACKEND PROPERTY STRINGS HW SIM)

# Optional debug-port traces, OFF by default:
#   PRISM_KIT_LOG_EVB        "[EVB]" EventMailbox send/receive traces.
#   PRISM_KIT_LOG_CONTROLLER "[Controller]" Controller trace output.
# Set one to ON below, or pass -D<NAME>=ON to CMake/west. These are plain
# defaults rather than cache entries so that editing them takes effect on the
# next incremental build; a -D value is cached and sticks until it is
# overridden or the build directory is cleaned.
if(NOT DEFINED PRISM_KIT_LOG_EVB)
  set(PRISM_KIT_LOG_EVB OFF)
endif()
if(NOT DEFINED PRISM_KIT_LOG_CONTROLLER)
  set(PRISM_KIT_LOG_CONTROLLER OFF)
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   ${CMAKE_CURRENT_SOURCE_DIR}/src
                                   ${PROJECT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/blink_app.cpp
                         src/controller_command_dispatch.cpp
                         src/hw/hw_task.cpp
                         src/hw/status_led.cpp
                         src/hw/strip_manager.cpp
                         src/hw/command_manager.cpp
                         src/hw/controller_command_sink.cpp
                         controller/controller.cpp
                         controller/instructions.cpp)

if(PRISM_KIT_LOG_EVB)
  target_compile_definitions(app PRIVATE PRISM_KIT_LOG_EVB)
endif()
if(PRISM_KIT_LOG_CONTROLLER)
  target_compile_definitions(app PRIVATE PRISM_KIT_LOG_CONTROLLER)
endif()

if(PRISM_KIT_APP_BACKEND STREQUAL "HW")
  target_sources(app PRIVATE src/prism_hw_backend.cpp src/app_hw.cpp)
elseif(PRISM_KIT_APP_BACKEND STREQUAL "SIM")
  message(FATAL_ERROR "PRISM_KIT_APP_BACKEND=SIM is not implemented yet.")
else()
  message(FATAL_ERROR
    "Unsupported PRISM_KIT_APP_BACKEND='${PRISM_KIT_APP_BACKEND}'.")
endif()

# Link APP against the reusable lower layers instead of treating them as source
# buckets inside the Zephyr app target.
target_link_libraries(app PRIVATE bal_implementation oshal_implementation protocol_implementation)