# BAL owns board resources and the application bootstrap hand-off.
add_library(bal_implementation OBJECT)

target_include_directories(bal_implementation
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(bal_implementation
	PRIVATE
		src/bootstrap.cpp
		src/seeeduino_xiao/pin_map.cpp
		src/led.cpp
		src/seeeduino_xiao/led.cpp
		src/ws2812.cpp
		src/seeeduino_xiao/ws2812.cpp)

# BAL depends only on the OSHAL implementation target; the required Zephyr
# compile environment propagates through OSHAL's public interface usage
# requirements.
target_link_libraries(bal_implementation PUBLIC oshal_implementation)