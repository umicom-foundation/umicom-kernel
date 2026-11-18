# Umicom Kernel
# File: tests/tcp_transmit/CMakeLists.txt
#
# PURPOSE:
#   Qualify transmit accounting without opening a socket or sending a packet.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
cmake_minimum_required(VERSION 3.24)
project(UmicomTcpTransmitContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-tcp-transmit-tests transmit_tests.c
    ../../kernel/tcp_transmit.c ../../kernel/tcp_sequence.c)
target_include_directories(umicom-tcp-transmit-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-tcp-transmit-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
foreach(case cycle refusal wrap model)
    add_test(NAME kernel.tcp_transmit.${case} COMMAND umicom-tcp-transmit-tests "${case}")
    set_tests_properties(kernel.tcp_transmit.${case} PROPERTIES TIMEOUT 20 LABELS "kernel;tcp;hosted;no-network")
endforeach()
