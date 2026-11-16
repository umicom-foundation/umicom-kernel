# Umicom Kernel
# File: tests/tcp_sequence/CMakeLists.txt
# PURPOSE: Qualify pure TCP sequence operations on the host and link them for RV64.
# AUTHOR AND ORGANISATION: Sammy Hegab / Umicom Foundation
# LICENCE: MIT
cmake_minimum_required(VERSION 3.24)
project(UmicomTcpSequenceContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-tcp-sequence-tests sequence_tests.c ../../kernel/tcp_sequence.c)
target_include_directories(umicom-tcp-sequence-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-tcp-sequence-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case compare advance length ack window model)
    add_test(NAME kernel.tcp_sequence.${case} COMMAND umicom-tcp-sequence-tests "${case}")
    set_tests_properties(kernel.tcp_sequence.${case} PROPERTIES TIMEOUT 20 LABELS "kernel;tcp;hosted;no-network")
endforeach()
set(UMICOM_SEQUENCE_CLANG "" CACHE FILEPATH "Optional Clang for RV64 freestanding link checks")
if(UMICOM_SEQUENCE_CLANG)
    foreach(level 0 2)
        add_test(NAME kernel.tcp_sequence.rv64_link.O${level} COMMAND "${CMAKE_COMMAND}"
            "-DCLANG=${UMICOM_SEQUENCE_CLANG}" "-DLEVEL=${level}"
            "-DSOURCE=${CMAKE_CURRENT_LIST_DIR}/../.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rv64-O${level}"
            -P "${CMAKE_CURRENT_LIST_DIR}/check_freestanding.cmake")
        set_tests_properties(kernel.tcp_sequence.rv64_link.O${level} PROPERTIES TIMEOUT 30 LABELS "kernel;tcp;freestanding;link-only")
    endforeach()
endif()
