# BAL owns board resources and the application bootstrap hand-off.
add_library(ws2812_bal OBJECT)

target_include_directories(ws2812_bal
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(ws2812_bal
	PRIVATE
		src/bootstrap.cpp
		src/board_led.cpp
		src/board_led_seeeduino_xiao.cpp
		src/board_ws2812.cpp
		src/board_ws2812_seeeduino_xiao.cpp)

# BAL still compiles inside Zephyr's build environment even though it now lands
# in the final image as an object target instead of a static archive.
target_link_libraries(ws2812_bal
	PRIVATE zephyr_interface
	PUBLIC ws2812_oshal)