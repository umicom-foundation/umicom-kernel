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
        "packet-admission.cases=19"
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
message(STATUS "Nineteen packet groups completed in the current diagnostic guest without a NIC")
