# Umicom Kernel typed VFS and object-cache-backed RAM filesystem.
# Storage, descriptors and namespace lifetimes are new owners above the existing
# allocators; the diagnostic program and its loader are reused without changes.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/vfs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/ramfs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/vfs_validation.c")
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.vfs_ramfs
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.vfs_ramfs PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_VFS_RAMFS_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
