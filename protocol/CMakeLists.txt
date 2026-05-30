# Protocol — TLV framing library
#
# Follows the same INTERFACE + OBJECT pattern used by OSHAL and BAL so
# consuming targets can inherit the protocol headers and implementation
# without Zephyr-library-mode warnings in application mode builds.

add_library(protocol_interface INTERFACE)
target_include_directories(protocol_interface
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(protocol_interface INTERFACE zephyr_interface)

add_library(protocol_implementation OBJECT)
target_include_directories(protocol_implementation
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(protocol_implementation PRIVATE src/protocol.cpp)
target_link_libraries(protocol_implementation PUBLIC protocol_interface)
