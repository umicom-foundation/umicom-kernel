# Umicom Kernel cooperative threads. These are new sources, not replacements
# for the user/process monitor, machine traps or channel implementation.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/thread_context.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/thread_probe.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/threads.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/thread_validation.c"
)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.cooperative_threads
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.cooperative_threads PROPERTIES
        TIMEOUT 15
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_COOPERATIVE_THREADS_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
