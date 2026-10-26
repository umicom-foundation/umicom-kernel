# Umicom Kernel | Sammy Hegab, Umicom Foundation | MIT
# Real freestanding metadata and reservation planning; synthetic host buffers.
cmake_minimum_required(VERSION 3.24)
project(UmicomDeviceTreeContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-dtb-layout-tests test_layout.c ../../kernel/device_tree_layout.c ../../kernel/device_tree_reader.c)
add_executable(umicom-boot-reservation-tests test_reservations.c
    ../../kernel/boot_reservations.c ../../kernel/address.c)
foreach(_target IN ITEMS umicom-dtb-layout-tests umicom-boot-reservation-tests)
    target_include_directories(${_target} PRIVATE ../../include)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
endforeach()
if(BUILD_TESTING)
    foreach(_case IN ITEMS valid empty future-compatible maximum arguments truncation magic
        version incompatible structure-range strings-range structure-alignment reservation-alignment
        section-overlap unterminated reservation-zero reservation-wrap reservation-overlap capacity names)
        add_test(NAME "kernel.device_tree.layout.${_case}" COMMAND umicom-dtb-layout-tests "${_case}")
        set_tests_properties("kernel.device_tree.layout.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
    foreach(_case IN ITEMS base inside kernel-overlap shared-page clip-low clip-high outside bridge
        ram-wrap ram-alignment tree-alignment tree-outside tree-overlap reservation-wrap
        reservation-empty capacity arguments)
        add_test(NAME "kernel.boot_reservations.${_case}" COMMAND umicom-boot-reservation-tests "${_case}")
        set_tests_properties("kernel.boot_reservations.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()
