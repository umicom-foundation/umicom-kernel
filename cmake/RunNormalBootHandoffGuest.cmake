#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/RunNormalBootHandoffGuest.cmake
# PURPOSE: Verify canonical handoff output from checked startup and recovery images.
# AUTHOR AND ORGANISATION: Sammy Hegab / Umicom Foundation
# LICENCE: MIT
#-----------------------------------------------------------------------------
foreach(mode IN ITEMS NORMAL SERVICE_RECOVERY FORCED_RECOVERY)
    if(NOT EXISTS "${${mode}}")
        message(FATAL_ERROR "The current ${mode} image is required for boot handoff qualification.")
    endif()
    execute_process(COMMAND "${QEMU}" -machine "virt,aclint=off" -bios "${${mode}}"
        -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic TIMEOUT 25)
    message("${mode} transcript\n${output}${diagnostic}")
    if(NOT "${result}" STREQUAL "0" OR output MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC")
        message(FATAL_ERROR "The ${mode} guest did not complete successfully (${result}).")
    endif()
    if(mode STREQUAL "FORCED_RECOVERY")
        if(NOT output MATCHES "recovery[.]reason=forced-recovery" OR
            NOT output MATCHES "UMICOM_KERNEL_RECOVERY_SELECTION_READY" OR
            output MATCHES "boot[.]handoff=|boot handoff:|boot[.]memory=reserved")
            message(FATAL_ERROR "Forced recovery reported memory or a handoff it did not initialize.")
        endif()
        continue()
    endif()
    string(REGEX MATCHALL "boot[.]handoff=validated" observed "${output}")
    list(LENGTH observed count)
    if(NOT count EQUAL 1 OR NOT output MATCHES "boot[.]memory=reserved[\r\n]+boot[.]handoff=validated" OR
        NOT output MATCHES "boot handoff: riscv64 ram=1 reserved=[1-9][0-9]*")
        message(FATAL_ERROR "The ${mode} guest did not report its admitted RISC-V handoff after memory reservation.")
    endif()
    if(mode STREQUAL "NORMAL" AND NOT output MATCHES "UMICOM_KERNEL_NORMAL_STARTUP_READY")
        message(FATAL_ERROR "The observed normal image did not complete startup.")
    endif()
    if(mode STREQUAL "SERVICE_RECOVERY" AND (NOT output MATCHES "recovery[.]reason=required-startup-service" OR
        NOT output MATCHES "UMICOM_KERNEL_RECOVERY_SELECTION_READY"))
        message(FATAL_ERROR "The observed service failure did not enter its dedicated recovery path.")
    endif()
endforeach()
