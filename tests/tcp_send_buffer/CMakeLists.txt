# Umicom Kernel
# File: tests/tcp_send_buffer/CMakeLists.txt
#
# PURPOSE:
#   Qualify owned send storage with hosted checks and optional freestanding links.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
cmake_minimum_required(VERSION 3.24)
project(UmicomTcpSendBufferContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-tcp-send-buffer-tests send_tests.c
    ../../kernel/tcp_send_buffer.c ../../kernel/tcp_transmit.c ../../kernel/tcp_sequence.c)
target_include_directories(umicom-tcp-send-buffer-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-tcp-send-buffer-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case cycle ownership capacity wrap refusal corrupt zeroing model)
    add_test(NAME kernel.tcp_send_buffer.${case} COMMAND umicom-tcp-send-buffer-tests "${case}")
    set_tests_properties(kernel.tcp_send_buffer.${case} PROPERTIES TIMEOUT 20 LABELS "kernel;tcp;hosted;no-network")
endforeach()
set(UMICOM_SEND_CLANG "" CACHE FILEPATH "Optional Clang for RV64 freestanding link checks")
if(UMICOM_SEND_CLANG)
    foreach(level 0 2)
        add_test(NAME kernel.tcp_send_buffer.rv64_link.O${level} COMMAND "${CMAKE_COMMAND}"
            "-DCLANG=${UMICOM_SEND_CLANG}" "-DLEVEL=${level}"
            "-DSOURCE=${CMAKE_CURRENT_LIST_DIR}/../.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rv64-O${level}"
            -P "${CMAKE_CURRENT_LIST_DIR}/check_freestanding.cmake")
        set_tests_properties(kernel.tcp_send_buffer.rv64_link.O${level} PROPERTIES TIMEOUT 30 LABELS "kernel;tcp;freestanding;link-only")
    endforeach()
endif()
