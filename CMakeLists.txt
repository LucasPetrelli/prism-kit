cmake_minimum_required(VERSION 3.20.0)

# Zephyr remains the build owner. This repository contributes sources,
# configuration, and documentation on top of the standard Zephyr application
# model instead of creating a parallel bare-metal build flow.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ws2812_samd21_led_driver)

# Keep the top-level entry point thin and compose the firmware from explicit
# layers so the dependency direction stays visible from the root build file.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/main.c)

add_subdirectory(hal)
add_subdirectory(osal)
add_subdirectory(bal)
add_subdirectory(app)
