# OSHAL owns the Zephyr-facing backend services and early init hooks.
add_library(oshal_interface INTERFACE)

target_include_directories(oshal_interface
	INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Keep the Zephyr compile environment behind an OSHAL-owned interface target so
# lower layers can inherit the required Cortex-M flags without taking a direct
# dependency on zephyr_interface by name.
target_link_libraries(oshal_interface INTERFACE zephyr_interface)

add_library(oshal_implementation OBJECT)

target_include_directories(oshal_implementation
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(oshal_implementation
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

# OSHAL consumes the Zephyr compile environment through its own interface
# target so the implementation choice stays hidden behind an OSHAL-named
# abstraction.
target_link_libraries(oshal_implementation PUBLIC oshal_interface)