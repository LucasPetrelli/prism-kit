# OSHAL owns the Zephyr-facing backend services and early init hooks.
add_library(ws2812_oshal OBJECT)

target_include_directories(ws2812_oshal
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(ws2812_oshal
	PRIVATE
		src/debug_port_zephyr.cpp
		src/debug_port_cdc_acm.cpp
		src/gpio_zephyr.cpp
		src/samd21_dmac_channel.cpp
		src/samd21.cpp
		src/pwm_samd21.cpp
		src/system_zephyr.c
		src/task/task_zephyr.cpp
		src/task/task_stats_zephyr.cpp
		src/time_zephyr.cpp)

# OSHAL is the only layer that compiles directly against Zephyr and SoC headers,
# so give that dependency to the object target explicitly.
target_link_libraries(ws2812_oshal PRIVATE zephyr_interface)