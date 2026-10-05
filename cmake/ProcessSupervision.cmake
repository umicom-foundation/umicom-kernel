# Umicom Kernel parent authority and coordinated process/IPC cleanup.
# Reuse the existing diagnostic and blocking-message executables. No new
# architecture entry, ELF loader, message queue or scheduler is introduced.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_supervisor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_supervision_validation.c")
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.process_supervision
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.process_supervision PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_PROCESS_SUPERVISION_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
