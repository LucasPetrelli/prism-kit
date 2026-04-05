# BAL owns board resources and the application bootstrap hand-off.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/bootstrap.cpp src/board_led.cpp src/board_led_seeeduino_xiao.cpp)