#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/FirmwareEntry.cmake
#
# Attach the machine-mode firmware bridge to every executable that uses the
# established Kernel linker script. Include this module after the modules that
# create diagnostic, normal, recovery, console, and storage acceptance images.
# Separately linked user programs have their own linker scripts and retain
# their existing entry/privilege contract.
#
# Discovering the linker-script dependency keeps ownership in one build helper:
# a new Kernel image receives the same bridge without copying a target list.
# The linker's entry-section assertions also refuse a Kernel image that omits
# the bridge rather than producing another silent firmware-address mismatch.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
#-----------------------------------------------------------------------------
include_guard(GLOBAL)

get_property(UMICOM_FIRMWARE_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(UMICOM_FIRMWARE_ATTACHED_TARGETS "")
foreach(UMICOM_FIRMWARE_TARGET IN LISTS UMICOM_FIRMWARE_TARGETS)
    get_target_property(UMICOM_FIRMWARE_TYPE "${UMICOM_FIRMWARE_TARGET}" TYPE)
    if(UMICOM_FIRMWARE_TYPE STREQUAL "EXECUTABLE")
        get_target_property(UMICOM_FIRMWARE_LINK_DEPENDENCIES
            "${UMICOM_FIRMWARE_TARGET}" LINK_DEPENDS)
        if("${UMICOM_KERNEL_LINKER_SCRIPT}" IN_LIST UMICOM_FIRMWARE_LINK_DEPENDENCIES)
            target_sources("${UMICOM_FIRMWARE_TARGET}" PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/../arch/riscv64/firmware_entry.S")
            list(APPEND UMICOM_FIRMWARE_ATTACHED_TARGETS "${UMICOM_FIRMWARE_TARGET}")
        endif()
    endif()
endforeach()

if(NOT UMICOM_FIRMWARE_ATTACHED_TARGETS)
    message(FATAL_ERROR
        "FirmwareEntry.cmake must follow the Kernel image definitions and linker-script dependency.")
endif()
