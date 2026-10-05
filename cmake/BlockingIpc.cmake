# Umicom Kernel blocking message operations and loaded producer/consumer proof.
# Existing nonblocking service numbers and Assembly entry paths stay intact.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_ipc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/blocking_ipc_validation.c")
# APPEND keeps all established monitor and scheduler feature definitions.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_BLOCKING_IPC=1)
add_executable(umicom-blocking-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/blocking_exchange/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/blocking_exchange/main.c")
target_include_directories(umicom-blocking-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_BLOCKING_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-blocking-program PRIVATE ${UMICOM_BLOCKING_COMPILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_BLOCKING_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/blocking_exchange/linker.ld")
target_link_options(umicom-blocking-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_BLOCKING_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-blocking-program PROPERTIES OUTPUT_NAME "umicom-blocking-exchange"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs" LINK_DEPENDS "${UMICOM_BLOCKING_LINKER}")
set(UMICOM_BLOCKING_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-blocking-exchange.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedBlockingProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_blocking_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_blocking_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_BLOCKING_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-blocking-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_blocking_program.S")
# The existing deferred nested-fault target collects all Kernel sources. Its
# copied carrier must also wait for the independent program's ELF to be linked.
function(UmicomBlockingNestedDependency)
    if(TARGET umicom-trap-nested)
        add_dependencies(umicom-trap-nested umicom-blocking-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomBlockingNestedDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.blocking_ipc
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.blocking_ipc PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_BLOCKING_IPC_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
