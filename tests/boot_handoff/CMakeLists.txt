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
