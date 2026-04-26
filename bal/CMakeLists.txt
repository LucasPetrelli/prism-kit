# BAL owns board resources and the application bootstrap hand-off.
add_library(bal_implementation OBJECT)

target_include_directories(bal_implementation
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(bal_implementation
	PRIVATE
		src/bootstrap.cpp
		src/board_pin_map_seeeduino_xiao.cpp
		src/board_led.cpp
		src/board_led_seeeduino_xiao.cpp
		src/board_ws2812.cpp
		src/board_ws2812_seeeduino_xiao.cpp)

# BAL depends only on the OSHAL implementation target; the required Zephyr
# compile environment propagates through OSHAL's public interface usage
# requirements.
target_link_libraries(bal_implementation PUBLIC oshal_implementation)