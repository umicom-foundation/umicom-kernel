#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/CvmEvidence.cmake
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
foreach(_required IN ITEMS MODE SOURCE KERNEL O0 O2 RECEIPT RECIPE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing CVM evidence argument: ${_required}")
    endif()
endforeach()
set(_identity "")
foreach(_relative IN ITEMS src/status.c src/vm.c src/bytecode.c src/comparison.c
    src/internal.h include/umicom_c/umicom_c.h include/umicom_c/comparison.h
    cmake/UmicomCRuntime.cmake)
    if(NOT EXISTS "${SOURCE}/${_relative}")
        message(FATAL_ERROR "Missing CVM source: ${_relative}")
    endif()
    file(SHA256 "${SOURCE}/${_relative}" _digest)
    string(APPEND _identity "umicom-c/${_relative}=${_digest}\n")
endforeach()
foreach(_relative IN ITEMS programs/cvm/main.c programs/cvm/entry.S
    programs/cvm/CMakeLists.txt programs/diagnostic/linker.ld cmake/CvmPrograms.cmake)
    file(SHA256 "${KERNEL}/${_relative}" _digest)
    string(APPEND _identity "kernel/${_relative}=${_digest}\n")
endforeach()
file(SHA256 "${RECIPE}" _recipe_digest)
file(SHA256 "${O0}" _o0_digest)
file(SHA256 "${O2}" _o2_digest)
string(APPEND _identity "recipe=${_recipe_digest}\no0=${_o0_digest}\no2=${_o2_digest}\n")
if(MODE STREQUAL "capture")
    file(WRITE "${RECEIPT}" "${_identity}")
elseif(MODE STREQUAL "verify")
    if(NOT EXISTS "${RECEIPT}")
        message(FATAL_ERROR "UMICOM_KERNEL_FAIL: build the CVM image before guest qualification")
    endif()
    file(READ "${RECEIPT}" _saved)
    if(NOT _saved STREQUAL _identity)
        message(FATAL_ERROR "UMICOM_KERNEL_FAIL: CVM source or image changed since the build receipt")
    endif()
else()
    message(FATAL_ERROR "Unknown CVM evidence mode")
endif()
