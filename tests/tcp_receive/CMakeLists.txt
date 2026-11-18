# Umicom Kernel
# File: tests/tcp_receive/CMakeLists.txt
#
# PURPOSE:
#   Qualify receive ownership without opening a socket or sending a packet.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
cmake_minimum_required(VERSION 3.24)
project(UmicomTcpReceiveContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-tcp-receive-tests receive_tests.c
    ../../kernel/tcp_receive.c ../../kernel/tcp_sequence.c)
target_include_directories(umicom-tcp-receive-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-tcp-receive-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case cycle capacity wrap ownership refusal corrupt zeroing model)
    add_test(NAME kernel.tcp_receive.${case} COMMAND umicom-tcp-receive-tests "${case}")
    set_tests_properties(kernel.tcp_receive.${case} PROPERTIES TIMEOUT 20 LABELS "kernel;tcp;hosted;no-network")
endforeach()

# The host tests cannot reveal an accidental dependency on a hosted library.
set(UMICOM_RECEIVE_CLANG "" CACHE FILEPATH "Optional Clang for RV64 freestanding link checks")
if(UMICOM_RECEIVE_CLANG)
    foreach(level 0 2)
        add_test(NAME kernel.tcp_receive.rv64_link.O${level} COMMAND "${CMAKE_COMMAND}"
            "-DCLANG=${UMICOM_RECEIVE_CLANG}" "-DLEVEL=${level}"
            "-DSOURCE=${CMAKE_CURRENT_LIST_DIR}/../.." "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rv64-O${level}"
            -P "${CMAKE_CURRENT_LIST_DIR}/check_freestanding.cmake")
        set_tests_properties(kernel.tcp_receive.rv64_link.O${level} PROPERTIES TIMEOUT 30 LABELS "kernel;tcp;freestanding;link-only")
    endforeach()
endif()
