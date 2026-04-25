# OSHAL owns the Zephyr-facing backend services and early init hooks.
add_library(ws2812_oshal OBJECT)

target_include_directories(ws2812_oshal
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(ws2812_oshal
	PRIVATE
		src/zephyr_debug_port.cpp
		src/zephyr_debug_port_cdc_acm.cpp
		src/zephyr_gpio.cpp
		src/zephyr_samd21_dmac_channel.cpp
		src/samd21.cpp
		src/samd21_pwm.cpp
		src/samd21_ws2812.cpp
		src/zephyr_system.c
		src/task/zephyr_task.cpp
		src/task/zephyr_task_stats.cpp
		src/zephyr_time.cpp)

# OSHAL is the only layer that compiles directly against Zephyr and SoC headers,
# so give that dependency to the object target explicitly.
target_link_libraries(ws2812_oshal PRIVATE zephyr_interface)