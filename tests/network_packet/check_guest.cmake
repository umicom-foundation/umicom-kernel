#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/network_packet/check_guest.cmake
#
# PURPOSE:
#   Require successful QEMU exit and complete ordered packet acceptance evidence.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
if(NOT EXISTS "${QEMU}" OR NOT DEFINED QEMU_ARGUMENTS OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "QEMU, QEMU_ARGUMENTS and OUTPUT are required")
endif()
execute_process(COMMAND "${QEMU}" ${QEMU_ARGUMENTS} -nic none
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 30)
file(WRITE "${OUTPUT}" "${_stdout}${_stderr}")
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Packet guest exit ${_result}; inspect ${OUTPUT}")
endif()
if("${_stdout}${_stderr}" MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
    message(FATAL_ERROR "Packet guest reported failure; inspect ${OUTPUT}")
endif()
set(_previous -1)
foreach(_marker IN ITEMS "UMICOM_KERNEL_BOOT_READY" "packet-admission-test=begin"
        "packet-admission.case=golden" "packet-admission.case=truncation"
        "packet-admission.case=checksum" "packet-admission.case=padding"
        "packet-admission.case=unaligned" "packet-admission.case=unsupported"
        "packet-admission.case=metadata-lifetime"
        # Earlier count retained as documentation: packet-admission.cases=7.
        # Require all old groups and all new transport groups before success.
        "packet-admission.case=udp-golden" "packet-admission.case=udp-refusals"
        "packet-admission.case=udp-padding" "packet-admission.case=udp-checksum-policy"
        # The previous eleven groups remain, followed by echo checks.
        # "packet-admission.cases=11"
        "packet-admission.case=icmp-golden" "packet-admission.case=icmp-reply"
        "packet-admission.case=icmp-refusals" "packet-admission.case=icmp-padding-lifetime"
        # Earlier total preserved; all fifteen groups still precede TCP.
        # "packet-admission.cases=15"
        "packet-admission.case=tcp-golden" "packet-admission.case=tcp-refusals"
        "packet-admission.case=tcp-zero-checksum" "packet-admission.case=tcp-padding-lifetime"
        # Earlier total retained; the same nineteen groups precede ARP.
        # "packet-admission.cases=19"
        "packet-admission.case=arp-golden" "packet-admission.case=arp-reply"
        "packet-admission.case=arp-refusals" "packet-admission.case=arp-padding-lifetime"
        # Earlier total retained; all twenty-three groups still precede IPv6.
        # "packet-admission.cases=23"
        "packet-admission.case=ipv6-golden" "packet-admission.case=ipv6-truncation"
        "packet-admission.case=ipv6-extension-policy" "packet-admission.case=ipv6-padding-lifetime"
        # Earlier total retained; the same twenty-seven groups precede IPv6 UDP.
        # "packet-admission.cases=27"
        "packet-admission.case=udp6-golden" "packet-admission.case=udp6-checksum"
        "packet-admission.case=udp6-framing" "packet-admission.case=udp6-padding-lifetime"
        # Earlier count retained; all thirty-one groups still precede ICMPv6 echo.
        # "packet-admission.cases=31"
        "packet-admission.case=icmp6-zero-reply" "packet-admission.case=icmp6-checksum"
        "packet-admission.case=icmp6-framing" "packet-admission.case=icmp6-padding-lifetime"
        # Previous count retained; all thirty-five groups precede TCP6.
        # "packet-admission.cases=35"
        "packet-admission.case=tcp6-zero-checksum" "packet-admission.case=tcp6-corruption"
        "packet-admission.case=tcp6-framing" "packet-admission.case=tcp6-padding-lifetime"
        # Earlier total retained; all thirty-nine groups precede neighbor captures.
        # "packet-admission.cases=39"
        "packet-admission.case=neighbor6-zero-advertisement" "packet-admission.case=neighbor6-checksum-framing"
        "packet-admission.case=neighbor6-dad-capture" "packet-admission.case=neighbor6-padding-lifetime"
        # Earlier total retained; forty-three earlier groups still execute.
        # "packet-admission.cases=43"
        "packet-admission.case=ethernet-plain" "packet-admission.case=ethernet-tags"
        "packet-admission.case=ethernet-refusals" "packet-admission.case=ethernet-lifetime"
        "packet-admission.cases=47"
        "UMICOM_KERNEL_PACKET_ADMISSION_READY" "UMICOM_KERNEL_END")
    string(FIND "${_stdout}" "${_marker}" _position)
    if(_position LESS_EQUAL _previous)
        message(FATAL_ERROR "Missing or unordered packet evidence ${_marker}; inspect ${OUTPUT}")
    endif()
    set(_previous ${_position})
endforeach()
# Previous status retained: Seven packet groups completed in the current diagnostic guest without a NIC.
# Previous report retained: Eleven packet groups completed in the current diagnostic guest without a NIC.
# Previous report retained: Fifteen packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Nineteen packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Twenty-three packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Twenty-seven packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Thirty-one packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Thirty-five packet groups completed in the current diagnostic guest without a NIC.
# Earlier status retained: Thirty-nine packet groups completed in the current diagnostic guest without a NIC.
# Previous status retained: Forty-three packet groups completed in the current diagnostic guest without a NIC.
message(STATUS "Forty-seven packet groups completed in the current diagnostic guest without a NIC")
