# Umicom Kernel copied message channels and checked user service integration.
# Existing sources keep their flags and the original execution path. The small
# monitor extension is enabled only when this service is linked into the Kernel.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/message_channel.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/message_service.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/message_validation.c"
)
# APPEND preserves any other source-local definitions. Native suites that test
# only the established monitor continue to build it without this optional service.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/user_monitor.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_MESSAGE_CHANNELS=1)

add_executable(umicom-message-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/message_exchange/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/message_exchange/main.c")
target_include_directories(umicom-message-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_MESSAGE_COMPILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-message-program PRIVATE ${UMICOM_MESSAGE_COMPILE_OPTIONS}
    -fno-vectorize -fno-slp-vectorize)
set(UMICOM_MESSAGE_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/message_exchange/linker.ld")
target_link_options(umicom-message-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_MESSAGE_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-message-program PROPERTIES OUTPUT_NAME "umicom-message-exchange"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_MESSAGE_LINKER}")
set(UMICOM_MESSAGE_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-message-exchange.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedMessageProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_message_program.S" @ONLY)
# A file dependency forces carrier reassembly after a program edit, not merely
# a build-order dependency which could leave old bytes embedded in the Kernel.
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_message_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_MESSAGE_EXECUTABLE_FILE}")
add_dependencies(umicom-kernel umicom-message-program)
target_sources(umicom-kernel PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_message_program.S")
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.message_channels
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.message_channels PROPERTIES
        TIMEOUT 15 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_MESSAGE_CHANNELS_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()
