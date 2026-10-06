# Umicom Kernel copied standard streams and foreground terminal integration.
# Existing file tokens, IPC services and context-switch Assembly remain intact.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_streams.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_terminal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/standard_streams_validation.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_STANDARD_STREAMS=1)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_runtime.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_TERMINAL=1)

add_executable(umicom-stream-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/stream_client/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/stream_client/main.c")
target_include_directories(umicom-stream-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_STREAM_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-stream-program PRIVATE ${UMICOM_STREAM_COMPILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_STREAM_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/stream_client/linker.ld")
target_link_options(umicom-stream-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_STREAM_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-stream-program PROPERTIES OUTPUT_NAME "umicom-stream-client"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_STREAM_LINKER}")
set(UMICOM_STREAM_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-stream-client.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedStreamProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_stream_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_stream_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_STREAM_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-stream-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_stream_program.S")
function(UmicomStreamNestedDependency)
    # The ordinary interactive target already copies the Kernel's dependencies.
    # The separate nested-fault target also carries these immutable ELF bytes.
    if(TARGET umicom-trap-nested-image)
        add_dependencies(umicom-trap-nested-image umicom-stream-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomStreamNestedDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.standard_streams
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.standard_streams PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_STANDARD_STREAMS_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
