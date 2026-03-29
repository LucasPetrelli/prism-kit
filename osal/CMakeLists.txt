# OSAL stays intentionally small. It should grow only when the application has
# a concrete portability need for another service.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_sources(app PRIVATE src/time_zephyr.cpp)