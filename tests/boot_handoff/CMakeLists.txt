#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/boot_handoff/CMakeLists.txt
#
# PURPOSE:
#   Build host-side malformed-input checks against the real freestanding boot reader.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
project(UmicomBootHandoffContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-boot-handoff-tests handoff_tests.c ../../kernel/boot_handoff.c ../../kernel/address.c)
target_include_directories(umicom-boot-handoff-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-boot-handoff-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS valid architectures unaligned truncation header counts maximum ram ram-overlap kernel reservations firmware firmware-refusal framebuffer framebuffer-refusal command overlap arguments)
        add_test(NAME "kernel.boot_handoff.${_case}" COMMAND umicom-boot-handoff-tests "${_case}")
        set_tests_properties("kernel.boot_handoff.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Producer cases use the same reader and checked-address implementation as boot.
add_executable(umicom-boot-handoff-write-tests write_tests.c
    ../../kernel/boot_handoff_write.c ../../kernel/boot_handoff.c ../../kernel/address.c)
target_include_directories(umicom-boot-handoff-write-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-boot-handoff-write-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden architectures roundtrip maximum unused unaligned tail capacity capacity_limit zero_capacity ram_count reserved_count text_termination text_control pixel_tag table_tag architecture_tag kernel_range ram_overlap reservation firmware framebuffer overlap_record overlap_counter counter_in_record input_alignment counter_alignment arguments)
        add_test(NAME "kernel.boot_handoff.write.${_case}" COMMAND umicom-boot-handoff-write-tests "${_case}")
        set_tests_properties("kernel.boot_handoff.write.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()
