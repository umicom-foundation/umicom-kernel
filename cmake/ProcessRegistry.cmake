# Umicom Kernel process registry and owner-scoped handles.
# This layer reuses the separately linked diagnostic and existing process APIs.
# It adds no architecture flags and changes no earlier test or implementation.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_registry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_registry_validation.c"
)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.process_registry
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.process_registry PROPERTIES
        TIMEOUT 15
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_PROCESS_REGISTRY_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
