# Umicom Kernel checked, frame-backed object caches.
# Each cache owns independently allocated frames. No established service is
# migrated here; those storage lifetimes remain available unchanged.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/object_cache.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/object_cache_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/object_cache.c")
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.object_caches
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.object_caches PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_OBJECT_CACHES_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
