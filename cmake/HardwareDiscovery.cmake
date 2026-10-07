# Umicom Kernel read-only firmware catalogue. Reuse the established boot-span
# inspector; do not replace the platform memory profile or any device driver.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/device_tree_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/device_tree_firmware.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/hardware_catalogue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/hardware_boot.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/hardware_validation.c")
# Existing independent native console tests retain their original link surface.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_HARDWARE_CATALOGUE=1)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.hardware_discovery
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.hardware_discovery PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_HARDWARE_DISCOVERY_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
