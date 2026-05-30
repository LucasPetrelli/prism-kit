cmake_minimum_required(VERSION 3.20.0)

# Zephyr remains the build owner. This repository contributes sources,
# configuration, and documentation on top of the standard Zephyr application
# model instead of creating a parallel bare-metal build flow.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(prism_kit)

# Keep the Zephyr-facing integration thin and compose the firmware from
# explicit layers. The Zephyr main() entry point resides in the OSHAL layer,
# while the root app target owns only the thin composition glue that wires APP
# and BAL into the OSHAL-owned handoff hook.

foreach(required_submodule IN ITEMS oshal bal)
	if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${required_submodule}/CMakeLists.txt")
		message(FATAL_ERROR
			"Required submodule '${required_submodule}' is missing or not initialized. "
			"Run 'git submodule update --init --recursive' from the repository root and retry.")
	endif()
endforeach()

target_sources(app PRIVATE src/boot_handoff_zephyr.cpp)

add_subdirectory(oshal)
add_subdirectory(bal)
add_subdirectory(protocol)
add_subdirectory(app)
