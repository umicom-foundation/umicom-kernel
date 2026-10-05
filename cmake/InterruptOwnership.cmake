# Umicom Kernel hart-local critical sections and interrupt-source ownership.
# Existing timer acknowledgement, traps and scheduler policy remain in service.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/interrupt_state.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/interrupts.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/interrupt_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/qemu-riscv64/interrupt_ownership.c"
)
# APPEND retains earlier source-local settings. The deferred nested-fault image
# uses these same files and receives the same readiness checks automatically.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/thread_context.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/process_execution.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_INTERRUPT_OWNERSHIP=1)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.interrupt_ownership
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.interrupt_ownership PROPERTIES
        TIMEOUT 20 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_INTERRUPT_OWNERSHIP_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
