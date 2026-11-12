#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/PacketAdmission.cmake
#
# PURPOSE:
#   Register current-image packet admission without a virtual network device.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------

if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.packet_admission
        COMMAND "${CMAKE_COMMAND}" "-DQEMU=${UMICOM_QEMU_RISCV64}"
            "-DQEMU_ARGUMENTS=${UMICOM_QEMU_RISCV64_ARGUMENTS}"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/packet-admission-guest.log"
            -P "${CMAKE_CURRENT_LIST_DIR}/../tests/network_packet/check_guest.cmake")
    set_tests_properties(kernel.riscv64.packet_admission PROPERTIES TIMEOUT 40
        FIXTURES_REQUIRED kernel_current_image LABELS "kernel;network-packet;guest;no-network")
endif()
