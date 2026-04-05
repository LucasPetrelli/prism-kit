# APP contains product logic only. It depends on BAL and OSHAL headers but keeps
# Zephyr-specific APIs out of the public application interface.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/blink_app.cpp)

# Link APP against the reusable lower layers instead of treating them as source
# buckets inside the Zephyr app target.
target_link_libraries(app PRIVATE ws2812_bal ws2812_oshal)