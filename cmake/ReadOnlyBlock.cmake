# Umicom Kernel modern read-only VirtIO MMIO block path.
# The raw file is synthetic test input. It is never attached as a writable guest
# disk, and it is not a filesystem or a copy of a host disk.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/virtio_block.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/block_platform_policy.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/block_console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/virtio_block_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/qemu-riscv64/block_device.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_READ_ONLY_BLOCK=1)

# Ship deterministic bytes so Windows users do not need a host compiler or a
# new script merely to generate a test disk. CMake tracks the copy as an input.
set(UMICOM_BLOCK_FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/virtio_block/fixture.raw")
file(SHA256 "${UMICOM_BLOCK_FIXTURE_SOURCE}" UMICOM_BLOCK_FIXTURE_HASH)
if(NOT UMICOM_BLOCK_FIXTURE_HASH STREQUAL "e5bea1290b3be59bdaf9f3a99d0be7527baefb3822b7d4b4e1c6c9736a4efa46")
    message(FATAL_ERROR "The synthetic read-only block fixture does not match its recorded SHA-256.")
endif()
set(UMICOM_BLOCK_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-read-only.raw")
configure_file("${UMICOM_BLOCK_FIXTURE_SOURCE}" "${UMICOM_BLOCK_FIXTURE}" COPYONLY)
# QEMU's -drive uses commas as separators inside one argument. Spaces work
# because the full value is quoted below; a comma in the build path does not.
if(UMICOM_BLOCK_FIXTURE MATCHES ",")
    message(FATAL_ERROR "The block test build path must not contain a comma (QEMU -drive separator).")
endif()
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.block_without_device
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS}
            -global virtio-mmio.force-legacy=false)
    set_tests_properties(kernel.riscv64.block_without_device PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_BLOCK_ABSENT_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    add_test(NAME kernel.riscv64.read_only_block
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS}
            -global virtio-mmio.force-legacy=false
            -drive "file=${UMICOM_BLOCK_FIXTURE},if=none,format=raw,id=umicom_read_test,readonly=on"
            -device "virtio-blk-device,drive=umicom_read_test")
    set_tests_properties(kernel.riscv64.read_only_block PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_READ_ONLY_BLOCK_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
