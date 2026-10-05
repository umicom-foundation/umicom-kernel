# Umicom Kernel machine trap-stack and return-state hardening.
# The original source remains present; trap.S explicitly retains its superseded
# interrupted-stack body. Only the separate fault image enables fault injection.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/trap_entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/trap_probe.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/trap_integrity.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/trap_guard.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/trap_hardening_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/qemu-riscv64/trap_emergency.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/qemu-riscv64/trap_emergency.S"
)

if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.trap_integrity
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.trap_integrity PROPERTIES
        TIMEOUT 20 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_TRAP_INTEGRITY_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()

# Finish the fault image after all root-directory includes have contributed
# their sources. Otherwise a future additive module could reach the normal
# image but leave the fault image with an incomplete source graph.
function(UmicomConfigureTrapFailureImage)
    get_target_property(UMICOM_TRAP_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_TRAP_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_TRAP_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_TRAP_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_TRAP_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_TRAP_LIBRARIES umicom-kernel LINK_LIBRARIES)
    add_executable(umicom-trap-nested-image ${UMICOM_TRAP_SOURCES})
    target_include_directories(umicom-trap-nested-image PRIVATE ${UMICOM_TRAP_INCLUDES})
    target_compile_options(umicom-trap-nested-image PRIVATE ${UMICOM_TRAP_COMPILE_OPTIONS})
    target_compile_definitions(umicom-trap-nested-image PRIVATE UMICOM_TRAP_NESTED_VALIDATION=1)
    if(UMICOM_TRAP_DEFINITIONS)
        target_compile_definitions(umicom-trap-nested-image PRIVATE ${UMICOM_TRAP_DEFINITIONS})
    endif()
    if(UMICOM_TRAP_LIBRARIES)
        target_link_libraries(umicom-trap-nested-image PRIVATE ${UMICOM_TRAP_LIBRARIES})
    endif()
    target_link_options(umicom-trap-nested-image PRIVATE ${UMICOM_TRAP_LINK_OPTIONS}
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-trap-nested.map")
    set_target_properties(umicom-trap-nested-image PROPERTIES
        OUTPUT_NAME "umicom-trap-nested" SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
    # Generated carrier sources retain their existing file dependencies and
    # source-local compiler flags. Build their producer executables first.
    add_dependencies(umicom-trap-nested-image umicom-diagnostic-program umicom-message-program)
    # The current-image fixture builds this dependency too. An obsolete fault
    # ELF therefore cannot bypass a failed current compilation.
    add_dependencies(umicom-kernel umicom-trap-nested-image)

    if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
        # The test ELF returns success ONLY for the exact injected nested fault.
        # An arbitrary crash cannot emit this evidence; WILL_FAIL is not used.
        add_test(NAME kernel.riscv64.trap_nested_fault
            COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
            -bios "$<TARGET_FILE:umicom-trap-nested-image>"
            -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot)
        set_tests_properties(kernel.riscv64.trap_nested_fault PROPERTIES
            TIMEOUT 20 FIXTURES_REQUIRED kernel_current_image
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_TRAP_NESTED_REJECTION_READY"
            FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
    endif()
endfunction()
cmake_language(DEFER CALL UmicomConfigureTrapFailureImage)
