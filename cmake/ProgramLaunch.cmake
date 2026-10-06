# Umicom Kernel structured program entry with explicit argv/environment copies.
# Reuse image ownership, terminal output and the existing frame entry. The
# two original run commands remain numeric. Sammy Hegab, Umicom Foundation. MIT.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/program_launch.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/program_launch_validation.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/process_supervisor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_runtime.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_PROGRAM_LAUNCH=1)
add_executable(umicom-launch-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/launch_client/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/launch_client/main.c")
target_include_directories(umicom-launch-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_LAUNCH_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-launch-program PRIVATE ${UMICOM_LAUNCH_COMPILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_LAUNCH_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/launch_client/linker.ld")
target_link_options(umicom-launch-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_LAUNCH_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-launch-program PROPERTIES OUTPUT_NAME "umicom-launch-client"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_LAUNCH_LINKER}")
set(UMICOM_LAUNCH_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-launch-client.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedLaunchProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_launch_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_launch_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_LAUNCH_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-launch-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_launch_program.S")
function(UmicomLaunchNestedDependency)
    if(TARGET umicom-trap-nested-image)
        add_dependencies(umicom-trap-nested-image umicom-launch-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomLaunchNestedDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.program_launch
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.program_launch PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_PROGRAM_LAUNCH_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
