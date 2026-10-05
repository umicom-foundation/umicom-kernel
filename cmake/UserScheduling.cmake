# Umicom Kernel timer-driven user scheduling. Reuse the existing loader, syscall
# policy and Assembly register body; only an explicit frame entry opts into slices.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_slice.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduling_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/user_slice.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/user_slice_support.S")
# APPEND preserves the existing IPC definitions and source-local flags.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/user_execution.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_USER_SLICES=1)

add_executable(umicom-scheduled-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/scheduled_work/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/scheduled_work/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/scheduled_work/registers.S")
target_include_directories(umicom-scheduled-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_SCHEDULED_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-scheduled-program PRIVATE ${UMICOM_SCHEDULED_OPTIONS}
    -fno-vectorize -fno-slp-vectorize)
set(UMICOM_SCHEDULED_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/scheduled_work/linker.ld")
target_link_options(umicom-scheduled-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_SCHEDULED_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-scheduled-program PROPERTIES
    OUTPUT_NAME "umicom-scheduled-work" SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_SCHEDULED_LINKER}")
set(UMICOM_SCHEDULED_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-scheduled-work.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedScheduledProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_scheduled_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_scheduled_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_SCHEDULED_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-scheduled-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_scheduled_program.S")
# TrapHardening creates its separate image at the end of configuration. Add
# this producer afterwards as well, retaining the existing deferred source list.
function(UmicomScheduledFaultImageDependency)
    if(TARGET umicom-trap-nested-image)
        add_dependencies(umicom-trap-nested-image umicom-scheduled-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomScheduledFaultImageDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.user_scheduling
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.user_scheduling PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_USER_SCHEDULING_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
