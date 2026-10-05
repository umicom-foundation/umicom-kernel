# Umicom Kernel owned region lifecycle and protected-stack proof.
# Reuse the existing physical allocator, mapper and supervisor entry. Their
# implementations and the old allocation/execution users remain unchanged.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/mapped_regions.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/mapped_region_payload.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/mapped_region_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/mapped_regions.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/mapped_region_probe.S")
# Probe code maps only its private text pages. Avoid compiler-owned jump tables
# or vector constants outside those pages; other sources retain their flags.
set_source_files_properties(kernel/mapped_region_payload.c PROPERTIES
    COMPILE_OPTIONS "-fno-jump-tables;-fno-vectorize;-fno-slp-vectorize")
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.mapped_regions
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.mapped_regions PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_MAPPED_REGIONS_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
