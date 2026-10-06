# Umicom Kernel native-service readiness, health leases and controlled replacement.
# New reporting hooks leave the old one-shot controller and all Assembly intact.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/service_manager.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/service_manager_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/service_console.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_scheduler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_MANAGED_SERVICES=1)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_SERVICE_CONSOLE=1)
add_executable(umicom-health-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/health_service/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/health_service/main.c")
target_include_directories(umicom-health-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_HEALTH_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-health-program PRIVATE ${UMICOM_HEALTH_COMPILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_HEALTH_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/health_service/linker.ld")
target_link_options(umicom-health-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_HEALTH_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-health-program PROPERTIES OUTPUT_NAME "umicom-health-service"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_HEALTH_LINKER}")
set(UMICOM_HEALTH_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-health-service.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedHealthProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_health_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_health_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_HEALTH_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-health-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_health_program.S")
function(UmicomHealthNestedDependency)
    if(TARGET umicom-trap-nested-image)
        add_dependencies(umicom-trap-nested-image umicom-health-program)
    endif()
endfunction()
cmake_language(DEFER CALL UmicomHealthNestedDependency)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.managed_services
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.managed_services PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_MANAGED_SERVICES_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
