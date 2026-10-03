# Umicom Kernel executable-loading build integration.
#
# The diagnostic is a separate RV64 ELF, with its own entry and load segments.
# The Kernel carries its bytes as a temporary boot input until a filesystem or
# boot archive can provide executable files. No conversion script is involved.
# Sammy Hegab, Umicom Foundation. MIT licence.

add_executable(umicom-diagnostic-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/diagnostic/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/diagnostic/main.c"
)
target_include_directories(umicom-diagnostic-program PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Reuse the selected architecture, freestanding contract and warning policy.
# The user image gets a different linker script, not a different compiler.
get_target_property(UMICOM_EXECUTABLE_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-diagnostic-program PRIVATE
    ${UMICOM_EXECUTABLE_COMPILE_OPTIONS})
set(UMICOM_EXECUTABLE_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/diagnostic/linker.ld")
target_link_options(umicom-diagnostic-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany
    -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_EXECUTABLE_LINKER}"
    -Wl,--build-id=none -Wl,--no-relax
)
set_target_properties(umicom-diagnostic-program PROPERTIES
    OUTPUT_NAME "umicom-diagnostic"
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_EXECUTABLE_LINKER}"
)
# The existing build uses a single-configuration Ninja preset. Configure the
# literal incbin path and add a file dependency, not only an ordering dependency:
# editing the diagnostic must reassemble the carrier and relink the Kernel.
set(UMICOM_EMBEDDED_EXECUTABLE_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-diagnostic.elf")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedExecutable.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_executable.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_executable.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_EMBEDDED_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-diagnostic-program)
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_executable.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/executable.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_image.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/executable_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/process_execution.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/executable_cache.S"
)
# FENCE.I requires Zifencei. Keep that extra ISA requirement explicit on the one
# primitive that uses it, rather than silently changing existing source flags.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/executable_cache.S"
    PROPERTIES COMPILE_OPTIONS "-march=rv64imac_zicsr_zifencei")

if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.executable_loading
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.executable_loading PROPERTIES
        TIMEOUT 15
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_EXECUTABLE_LOADING_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
