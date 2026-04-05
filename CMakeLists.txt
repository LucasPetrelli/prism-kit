cmake_minimum_required(VERSION 3.20.0)

# Zephyr remains the build owner. This repository contributes sources,
# configuration, and documentation on top of the standard Zephyr application
# model instead of creating a parallel bare-metal build flow.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ws2812_samd21_led_driver)

# Keep the Zephyr-facing integration thin and compose the firmware from
# explicit layers. The Zephyr main() entry point now resides in the OSHAL
# layer, while this root build file continues to define the layer structure.

add_subdirectory(oshal)
add_subdirectory(bal)
add_subdirectory(app)
