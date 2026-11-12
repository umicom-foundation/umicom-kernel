#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/network_packet/CMakeLists.txt
#
# PURPOSE:
#   Build host contracts against the freestanding packet admission implementation.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
project(UmicomNetworkPacketContracts LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
add_executable(umicom-network-packet-tests packet_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-packet-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-packet-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden padding unaligned minimum maximum truncation
        capture_limit ether_type version header_length total_length reserved_flag
        fragments checksum payload_policy overlap output_alignment address_wrap arguments)
        add_test(NAME "kernel.network_packet.${_case}" COMMAND umicom-network-packet-tests "${_case}")
        set_tests_properties("kernel.network_packet.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Exercise the transport API separately while retaining every IPv4 contract.
add_executable(umicom-network-udp-tests udp_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-udp-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-udp-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden empty even maximum padding unaligned truncation
        short_header length_min short_length long_length protocol ip_checksum fragment
        absent_allowed absent_required corruption source_address destination_address
        zero_checksum_encoding ports arguments overlap output_alignment address_wrap
        lifetime capture_limit)
        add_test(NAME "kernel.network_udp.${_case}" COMMAND umicom-network-udp-tests "${_case}")
        set_tests_properties("kernel.network_udp.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()
