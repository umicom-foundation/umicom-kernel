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

# Windowed allocation uses the same production allocator; retain every earlier
# unrestricted-run case to detect changes to its long-standing contract.
add_executable(umicom-frame-window-tests frame_window_tests.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-frame-window-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-frame-window-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case uninitialised null-output zero-count zero-alignment nonpower-alignment empty-window reverse-window unaligned-start unaligned-end below-ram above-ram disjoint-low disjoint-high whole single middle exact-end count-too-large no-fallback absolute-alignment alignment-no-fit largest-alignment largest-alignment-zero reserved-start reserved-middle allocated-start allocated-middle fragmented reuse mixed-whole partial-release top-address maximum-capacity bitmap-boundary output-preserved exhausted)
        add_test(NAME kernel.physical_memory.window.${_case} COMMAND umicom-frame-window-tests ${_case})
        set_tests_properties(kernel.physical_memory.window.${_case} PROPERTIES
            TIMEOUT 10 LABELS "kernel;physical-memory;window;host;no-devices")
    endforeach()
endif()

# Report tests link the real allocator and public observation service. Every
# case checks unchanged ownership, including across bitmap word boundaries.
add_executable(umicom-window-report-tests window_report_tests.c
    ../../kernel/physical_window_report.c ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-window-report-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-window-report-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(mode uninitialised null-output zero-alignment nonpower-alignment empty reverse unaligned-start unaligned-end below-ram above-ram whole single subwindow reserved allocated mixed fragmented alignment-prefix alignment-none first-tie aligned-tie largest-alignment zero-address top-address maximum-capacity after-free after-unreserve window-exclusion no-reservation bitmap-boundary repeat all-reserved all-allocated)
        add_test(NAME kernel.physical_memory.report.${mode} COMMAND umicom-window-report-tests ${mode})
        set_tests_properties(kernel.physical_memory.report.${mode} PROPERTIES
            TIMEOUT 15 LABELS "kernel;physical-memory;report;host;no-devices")
    endforeach()
endif()


# The boundary suite preserves all run/window/report cases and adds independent
# model comparison; no actual RAM or hardware device is touched.
add_executable(umicom-frame-boundary-tests frame_boundary_tests.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-frame-boundary-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-frame-boundary-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(mode uninitialised null-output zero-count zero-alignment nonpower-alignment nonpower-boundary empty-window reverse-window unaligned-start unaligned-end below-ram above-ram disabled single-boundary crossing-prefix exact-end exact-bucket count-too-large window-no-fit absolute-origin alignment-and-boundary alignment-larger largest-boundary largest-alignment zero-address top-address reserved-prefix allocated-prefix fragmented reuse bitmap-boundary maximum-capacity exhaustive-zero exhaustive-one exhaustive-three exhaustive-five)
        add_test(NAME kernel.physical_memory.boundary.${mode} COMMAND umicom-frame-boundary-tests ${mode})
        set_tests_properties(kernel.physical_memory.boundary.${mode} PROPERTIES
            TIMEOUT 45 LABELS "kernel;physical-memory;boundary;host;no-devices")
    endforeach()
endif()


# Boundary reports reuse the production scanner and a separately expressed
# candidate oracle; existing allocation and report cases remain registered.
add_executable(umicom-boundary-report-tests boundary_report_tests.c
    ../../kernel/physical_window_report.c ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-boundary-report-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-boundary-report-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(mode uninitialised null-output zero-alignment nonpower-alignment nonpower-boundary empty reverse unaligned-start unaligned-end below-ram above-ram disabled single-boundary crossing-prefix exact-bucket partial-buckets absolute-alignment alignment-larger no-aligned-start fragmented reserved allocated mixed zero-address top-address largest-boundary largest-alignment maximum-capacity after-release exhaustive-zero exhaustive-one exhaustive-three exhaustive-five)
        add_test(NAME kernel.physical_memory.boundary_report.${mode} COMMAND umicom-boundary-report-tests ${mode})
        set_tests_properties(kernel.physical_memory.boundary_report.${mode} PROPERTIES
            TIMEOUT 45 LABELS "kernel;physical-memory;boundary-report;host;no-devices")
    endforeach()
endif()
