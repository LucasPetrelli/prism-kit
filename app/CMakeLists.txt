# APP contains product logic only. It depends on BAL and OSAL headers but keeps
# Zephyr-specific APIs out of the public application interface.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/blink_app.cpp)