#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/physical_memory/CMakeLists.txt
#
# PURPOSE:
#   Build hosted physical run contracts against the production Kernel allocator.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
project(UmicomPhysicalFrameContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-frame-run-tests frame_run_tests.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-frame-run-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-frame-run-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS
        uninitialised-allocate
        uninitialised-free
        null-output
        zero-count
        zero-alignment
        nonpower-alignment
        maximum-count
        largest-alignment
        largest-alignment-zero
        single
        pair
        whole-range
        last-frame
        physical-alignment
        reservation-barrier
        allocation-barrier
        fragmentation
        refused-output
        free-zero
        free-unaligned
        free-below
        free-end
        free-overrun
        free-largest-count
        free-unallocated
        free-reserved-tail
        free-hole-tail
        free-hole-middle
        free-repeat
        partial-release
        mixed-single-run
        alignment-one
        alignment-two
        alignment-eight
        alignment-large
        top-address
        maximum-capacity
        reuse-lowest
        reinitialise
        invalid-reinitialise)
        add_test(NAME "kernel.physical_memory.run.${_case}"
            COMMAND umicom-frame-run-tests "${_case}")
        set_tests_properties("kernel.physical_memory.run.${_case}" PROPERTIES
            TIMEOUT 10 LABELS "kernel;physical-memory;host;no-devices")
    endforeach()
endif()
