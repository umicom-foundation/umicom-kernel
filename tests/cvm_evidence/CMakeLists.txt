#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/cvm_evidence/CMakeLists.txt
#
# PURPOSE:
#   Check stale external runtime evidence without executing fixture images.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(UmicomCvmEvidence LANGUAGES NONE)
    include(CTest)
endif()
foreach(case IN ITEMS unchanged source-edit image-edit recipe-edit receipt-missing source-missing unknown-mode)
    add_test(NAME "kernel.cvm.evidence.${case}"
        COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${case}"
            "-DEVIDENCE=${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CvmEvidence.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check.cmake")
    set_tests_properties("kernel.cvm.evidence.${case}" PROPERTIES
        TIMEOUT 20 LABELS "kernel;cvm;identity;host")
endforeach()
