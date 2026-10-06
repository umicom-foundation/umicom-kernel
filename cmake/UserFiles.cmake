# Umicom Kernel process-owned file services. Deferred requests reuse the existing
# VFS and checked copies without weakening allocation/interrupt admission gates.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_files.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_file_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_files_validation.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_FILE_SERVICES=1)
add_executable(umicom-file-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/file_client/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/file_client/main.c")
target_include_directories(umicom-file-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_FILE_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-file-program PRIVATE ${UMICOM_FILE_COMPILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_FILE_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/file_client/linker.ld")
target_link_options(umicom-file-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_FILE_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-file-program PROPERTIES OUTPUT_NAME "umicom-file-client"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_FILE_LINKER}")
set(UMICOM_FILE_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-file-client.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedFileProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_file_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_file_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_FILE_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-file-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_file_program.S")
# The separate nested-fault image obtains all Kernel sources at deferred time.
# Its carrier also needs the new program's linked bytes, not just a filename.
function(UmicomFileNestedDependency)
    if(TARGET umicom-trap-nested-image)
        add_dependencies(umicom-trap-nested-image umicom-file-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomFileNestedDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.file_services
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.file_services PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_FILE_SERVICES_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
