#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/RunCvmGuest.cmake
#
# PURPOSE:
#   Bind CVM guest observations to their external runtime source and linked images.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------
set(MODE verify)
include("${CMAKE_CURRENT_LIST_DIR}/CvmEvidence.cmake")
execute_process(COMMAND "${QEMU}" ${QEMU_ARGUMENTS}
    RESULT_VARIABLE _status OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 25)
message("${_out}")
message("${_err}")
if(NOT "${_status}" STREQUAL "0" OR NOT _out MATCHES "UMICOM_KERNEL_CVM_READY"
    OR _out MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC")
    message(FATAL_ERROR "UMICOM_KERNEL_FAIL: CVM guest did not complete (${_status})")
endif()
if(DEFINED HOST_ARTIFACT)
    # The optional program must run at both optimization levels. Generic
    # readiness alone cannot certify that either new fixture was executed.
    foreach(_selector IN ITEMS 73 74)
        string(REGEX MATCHALL "cvm[.]case=${_selector} complete" _observed "${_out}")
        list(LENGTH _observed _count)
        if(NOT _count EQUAL 2)
            message(FATAL_ERROR "UMICOM_KERNEL_FAIL: host artifact case ${_selector} did not complete at O0 and O2")
        endif()
    endforeach()
endif()
