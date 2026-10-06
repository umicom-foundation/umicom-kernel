# Umicom Kernel console input and trusted interactive front end.
# Reuse all existing file and execution services, including their source-local
# feature definitions. Only the explicitly named console ELF waits for input.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_line.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_runtime.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell_validation.c")

if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    # This image injects commands into the real editor/command engine. It never
    # waits for host stdin, so the existing current-image fixture stays usable.
    add_test(NAME kernel.riscv64.console_shell
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.console_shell PROPERTIES
        TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_CONSOLE_SHELL_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()

option(UMICOM_BUILD_INTERACTIVE_CONSOLE "Build the separate RAM-only interactive console image" ON)
function(UmicomConfigureInteractiveConsole)
    if(NOT UMICOM_BUILD_INTERACTIVE_CONSOLE)
        return()
    endif()
    # Deferred construction sees every additive module. This prevents the
    # interactive build from silently lagging behind the normal Kernel source.
    get_target_property(UMICOM_CONSOLE_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_CONSOLE_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_CONSOLE_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_CONSOLE_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_CONSOLE_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_CONSOLE_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_CONSOLE_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    add_executable(umicom-console-image ${UMICOM_CONSOLE_SOURCES})
    target_include_directories(umicom-console-image PRIVATE ${UMICOM_CONSOLE_INCLUDES})
    target_compile_options(umicom-console-image PRIVATE ${UMICOM_CONSOLE_OPTIONS})
    target_compile_definitions(umicom-console-image PRIVATE UMICOM_KERNEL_INTERACTIVE_CONSOLE=1)
    if(UMICOM_CONSOLE_DEFINITIONS)
        target_compile_definitions(umicom-console-image PRIVATE ${UMICOM_CONSOLE_DEFINITIONS})
    endif()
    if(UMICOM_CONSOLE_LIBRARIES)
        target_link_libraries(umicom-console-image PRIVATE ${UMICOM_CONSOLE_LIBRARIES})
    endif()
    target_link_options(umicom-console-image PRIVATE ${UMICOM_CONSOLE_LINK_OPTIONS}
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-console.map")
    set_target_properties(umicom-console-image PROPERTIES OUTPUT_NAME "umicom-console"
        SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
    if(UMICOM_CONSOLE_DEPENDENCIES)
        # Includes carrier producers and the independent nested-fault image.
        # Capture BEFORE adding the reverse dependency below to avoid a cycle.
        add_dependencies(umicom-console-image ${UMICOM_CONSOLE_DEPENDENCIES})
    endif()
    # The ordinary fixture's incremental build also keeps the interactive image
    # current. It does not execute that indefinitely waiting image as a CTest.
    add_dependencies(umicom-kernel umicom-console-image)
endfunction()
cmake_language(DEFER CALL UmicomConfigureInteractiveConsole)
