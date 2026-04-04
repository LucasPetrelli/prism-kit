# OSHAL owns the Zephyr-facing backend services and early init hooks.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/gpio_zephyr.c src/pwm_samd21.c src/system_zephyr.c src/time_zephyr.cpp)