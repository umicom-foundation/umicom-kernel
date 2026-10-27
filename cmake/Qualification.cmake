#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/Qualification.cmake
# PURPOSE: Bind current-build qualification to observable source and image hashes.
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
# LICENCE: MIT
#-----------------------------------------------------------------------------
set(_kernel_evidence "${CMAKE_CURRENT_LIST_DIR}/QualificationEvidence.cmake")
add_custom_target(umicom-kernel-evidence ALL
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DBINARY=${PROJECT_BINARY_DIR}"
        -DMODE=capture "-DCOMPILER=${CMAKE_C_COMPILER}" "-DCOMPILER_ID=${CMAKE_C_COMPILER_ID}"
        "-DCOMPILER_VERSION=${CMAKE_C_COMPILER_VERSION}" "-DARCHITECTURE=${CMAKE_SYSTEM_PROCESSOR}"
        -DABI=lp64 "-DMACHINE=virt,aclint=off" "-DQEMU=${UMICOM_QEMU_RISCV64}"
        -P "${_kernel_evidence}"
    DEPENDS umicom-kernel
    VERBATIM
    COMMENT "Capture Kernel source and ELF identities")
if(BUILD_TESTING)
    add_test(NAME kernel.qualification.identity
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DBINARY=${PROJECT_BINARY_DIR}"
            -DMODE=verify -P "${_kernel_evidence}")
    set_tests_properties(kernel.qualification.identity PROPERTIES TIMEOUT 60 LABELS "kernel;identity")
    if(UMICOM_QEMU_RISCV64)
        set_tests_properties(kernel.qualification.identity PROPERTIES FIXTURES_REQUIRED kernel_current_image)
    endif()
    add_subdirectory(tests/qualification)
endif()
