#-----------------------------------------------------------------------------
# Umicom Kernel
# File: cmake/CvmPrograms.cmake
#
# PURPOSE:
#   Connect the bounded CVM runtime to the native user-program contract.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#---------------------------------------------------------------------------
option(UMICOM_KERNEL_CVM "Embed and qualify optional Umicom C user programs" OFF)
if(UMICOM_KERNEL_CVM)
    add_subdirectory(programs/cvm)
    set(UMICOM_CVM_O0_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/cvm/bin/umicom-cvm-o0.elf")
    set(UMICOM_CVM_O2_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/cvm/bin/umicom-cvm-o2.elf")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/EmbeddedCvm.S.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_cvm.S" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_cvm.S"
        PROPERTIES OBJECT_DEPENDS "${UMICOM_CVM_O0_FILE};${UMICOM_CVM_O2_FILE}")
    target_sources(umicom-kernel PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_cvm.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/kernel/cvm_validation.c")
    target_compile_definitions(umicom-kernel PRIVATE UMICOM_KERNEL_CVM=1)
    add_dependencies(umicom-kernel umicom-cvm-user-o0 umicom-cvm-user-o2)
    set(_cvm_receipt "${CMAKE_CURRENT_BINARY_DIR}/cvm-source-receipt.txt")
    set(_cvm_recipe "${CMAKE_CURRENT_BINARY_DIR}/cvm-build-recipe.txt")
    file(GENERATE OUTPUT "${_cvm_recipe}" CONTENT
        "compiler=${CMAKE_C_COMPILER}\nid=${CMAKE_C_COMPILER_ID}\nversion=${CMAKE_C_COMPILER_VERSION}\ntarget=${CMAKE_C_COMPILER_TARGET}\ntype=${CMAKE_BUILD_TYPE}\ncflags=${CMAKE_C_FLAGS}\ndebug=${CMAKE_C_FLAGS_DEBUG}\nrelease=${CMAKE_C_FLAGS_RELEASE}\nrelwithdebinfo=${CMAKE_C_FLAGS_RELWITHDEBINFO}\nminsizerel=${CMAKE_C_FLAGS_MINSIZEREL}\nasm=${CMAKE_ASM_FLAGS}\nlink=${CMAKE_EXE_LINKER_FLAGS}\n")
    set(_cvm_evidence_args
        "-DSOURCE=${UMICOM_C_SOURCE_DIR}" "-DKERNEL=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DO0=${UMICOM_CVM_O0_FILE}" "-DO2=${UMICOM_CVM_O2_FILE}"
        "-DRECEIPT=${_cvm_receipt}" "-DRECIPE=${_cvm_recipe}")
    add_custom_target(umicom-cvm-evidence
        COMMAND "${CMAKE_COMMAND}" -DMODE=capture ${_cvm_evidence_args}
            -P "${CMAKE_CURRENT_LIST_DIR}/CvmEvidence.cmake"
        DEPENDS umicom-cvm-user-o0 umicom-cvm-user-o2 VERBATIM)
    add_dependencies(umicom-kernel umicom-cvm-evidence)
    if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
        add_test(NAME kernel.riscv64.cvm
            COMMAND "${CMAKE_COMMAND}" ${_cvm_evidence_args}
                "-DQEMU=${UMICOM_QEMU_RISCV64}"
                "-DQEMU_ARGUMENTS=${UMICOM_QEMU_RISCV64_ARGUMENTS}"
                -P "${CMAKE_CURRENT_LIST_DIR}/RunCvmGuest.cmake")
        set_tests_properties(kernel.riscv64.cvm PROPERTIES TIMEOUT 30
            FIXTURES_REQUIRED kernel_current_image
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_CVM_READY"
            FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC"
            LABELS "kernel;cvm;guest")
    endif()
endif()

# The identity contract is also testable without launching a Kernel guest.
if(UMICOM_KERNEL_CVM AND BUILD_TESTING)
    add_subdirectory(tests/cvm_evidence)
endif()
