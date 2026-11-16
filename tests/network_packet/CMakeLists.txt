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

# Echo admission extends the same production parser without replacing UDP tests.
add_executable(umicom-network-icmp-tests icmp_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-icmp-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-icmp-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden reply empty even maximum padding unaligned truncation short_header protocol type code ip_checksum fragment corruption zero_checksum address_scope identifiers arguments overlap output_alignment address_wrap lifetime capture_limit)
        add_test(NAME "kernel.network_icmp.${_case}" COMMAND umicom-network-icmp-tests "${_case}")
        set_tests_properties("kernel.network_icmp.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()


# TCP is a capture contract; all earlier IPv4, UDP and ICMP checks remain.
add_executable(umicom-network-tcp-tests tcp_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-tcp-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-tcp-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden empty even maximum padding unaligned truncation short_header
        data_offset options option_truncation protocol ip_checksum fragments corruption source_address
        destination_address checksum_zero flags reserved ports sequence window_urgent extra_ip_payload
        arguments overlap output_alignment address_wrap lifetime capture_limit)
        add_test(NAME "kernel.network_tcp.${_case}" COMMAND umicom-network-tcp-tests "${_case}")
        set_tests_properties("kernel.network_tcp.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# ARP is a separate Ethernet payload; preserve every earlier protocol group.
add_executable(umicom-network-arp-tests arp_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-arp-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-arp-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden reply padding maximum unaligned truncation capture_limit ether_type hardware_type protocol_type hardware_length protocol_length opcode network_order addresses sender_mismatch arguments overlap output_alignment address_wrap lifetime)
        add_test(NAME "kernel.network_arp.${_case}" COMMAND umicom-network-arp-tests "${_case}")
        set_tests_properties("kernel.network_arp.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# IPv6 base-header observations add to, rather than replace, earlier contracts.
add_executable(umicom-network-ipv6-tests ipv6_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-ipv6-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-ipv6-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden tcp icmpv6 empty no_next no_next_payload padding maximum unaligned truncation capture_limit payload_limit payload_truncation ether_type version fragments extensions jumbogram network_order addresses traffic flow hop lifetime arguments overlap output_alignment address_wrap transport_scope)
        add_test(NAME "kernel.network_ipv6.${_case}" COMMAND umicom-network-ipv6-tests "${_case}")
        set_tests_properties("kernel.network_ipv6.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Ordinary IPv6 UDP checksum validation preserves every earlier capture group.
add_executable(umicom-network-udp6-tests udp6_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-udp6-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-udp6-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden empty even maximum padding unaligned truncation short_header length_min short_length long_length protocol fragment extension zero_checksum corruption source_address destination_address zero_encoding ports metadata_scope pseudo_length extra_ip_payload arguments overlap output_alignment address_wrap lifetime capture_limit)
        add_test(NAME "kernel.network_udp6.${_case}" COMMAND umicom-network-udp6-tests "${_case}")
        set_tests_properties("kernel.network_udp6.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# ICMPv6 echo has a different checksum policy from both IPv4 echo and IPv6 UDP.
add_executable(umicom-network-icmp6-tests icmp6_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-icmp6-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-icmp6-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden reply empty even maximum padding unaligned truncation short-header protocol fragment extension type code corruption source-address destination-address zero-checksum valid-zero ipv4-scope udp-scope identity metadata-scope pseudo-length arguments overlap output-alignment address-wrap lifetime capture-limit)
        add_test(NAME "kernel.network_icmp6.${_case}" COMMAND umicom-network-icmp6-tests "${_case}")
        set_tests_properties("kernel.network_icmp6.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# TCP6 reuses the IPv6 checksum primitive; existing protocol tests remain active.
add_executable(umicom-network-tcp6-tests tcp6_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-tcp6-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-tcp6-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden empty even maximum padding unaligned truncation short-header data-offset options option-truncation protocol fragment extension corruption source-address destination-address zero-checksum valid-zero flags reserved ports sequence window-urgent extra-payload wrong-pseudo-protocol metadata-scope arguments overlap output-alignment address-wrap lifetime capture-limit)
        add_test(NAME "kernel.network_tcp6.${_case}" COMMAND umicom-network-tcp6-tests "${_case}")
        set_tests_properties("kernel.network_tcp6.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Neighbor captures add bounded option/address checks without changing prior protocols.
add_executable(umicom-network-neighbor6-tests neighbor6_tests.c
    ../../kernel/network_packet.c ../../kernel/address.c)
target_include_directories(umicom-network-neighbor6-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-network-neighbor6-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
if(BUILD_TESTING)
    foreach(_case IN ITEMS golden advertisement valid-zero dad no-option padding maximum unknown-option irrelevant-option unknown-long relevant-length duplicate-option zero-option short-option option-overrun target-multicast dad-option dad-destination dad-prefix advertisement-multicast flags reserved hop code type protocol fragment extension checksum pseudo-addresses truncation short-header unaligned overlap output-alignment address-wrap lifetime capture-limit arguments)
        add_test(NAME "kernel.network_neighbor6.${_case}" COMMAND umicom-network-neighbor6-tests "${_case}")
        set_tests_properties("kernel.network_neighbor6.${_case}" PROPERTIES TIMEOUT 10)
    endforeach()
endif()
