cmake_minimum_required(VERSION 3.20.0)

# Zephyr remains the build owner. This repository contributes sources,
# configuration, and documentation on top of the standard Zephyr application
# model instead of creating a parallel bare-metal build flow.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sentient_mask)

# Keep the Zephyr-facing integration thin and compose the firmware from
# explicit layers. The Zephyr main() entry point resides in the OSHAL layer,
# while the root app target owns only the thin composition glue that wires APP
# and BAL into the OSHAL-owned handoff hook.

target_sources(app PRIVATE src/boot_handoff_zephyr.cpp)

add_subdirectory(oshal)
add_subdirectory(bal)
add_subdirectory(app)
