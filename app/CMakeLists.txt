# APP contains product logic only. It depends on BAL and OSHAL headers but keeps
# Zephyr-specific APIs out of the public application interface.
set(PRISM_KIT_APP_BACKEND "HW" CACHE STRING "Prism Kit APP backend")
set_property(CACHE PRISM_KIT_APP_BACKEND PROPERTY STRINGS HW SIM)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                   ${PROJECT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/blink_app.cpp src/task_runtime_reporter.cpp)

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
target_link_libraries(app PRIVATE bal_implementation oshal_implementation)