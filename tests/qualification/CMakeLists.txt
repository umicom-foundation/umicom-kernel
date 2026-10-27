# Umicom Kernel | Sammy Hegab, Umicom Foundation | MIT
# Host-only evidence fixtures never execute their synthetic ELF bytes.
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(UmicomKernelEvidenceContracts LANGUAGES NONE)
    include(CTest)
endif()
foreach(_case unchanged source-edit source-added source-removed image-edit image-added image-missing corrupt-evidence missing-images unknown-mode unrelated-fixture)
    add_test(NAME "kernel.evidence.${_case}"
        COMMAND "${CMAKE_COMMAND}" "-DCASE=${_case}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${_case}"
            "-DEVIDENCE=${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/QualificationEvidence.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_evidence.cmake")
    set_tests_properties("kernel.evidence.${_case}" PROPERTIES TIMEOUT 30 LABELS "kernel;identity;host")
endforeach()
