# Umicom Kernel normal startup, bounded native jobs and independent recovery.
# Existing diagnostic and interactive images retain their cumulative checks.
# Sammy Hegab, Umicom Foundation. MIT licence.
target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/boot_services.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/boot_services_validation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/boot_memory.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/recovery_console.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/normal_boot.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_runtime.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_STARTUP_SERVICES=1)

if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(NAME kernel.riscv64.boot_services
        COMMAND "${UMICOM_QEMU_RISCV64}" ${UMICOM_QEMU_RISCV64_ARGUMENTS})
    set_tests_properties(kernel.riscv64.boot_services PROPERTIES TIMEOUT 30
        FIXTURES_REQUIRED kernel_current_image
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_BOOT_SERVICES_READY"
        FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED")
endif()

option(UMICOM_BUILD_SYSTEM_IMAGES "Build normal-system and independent recovery images" ON)
function(UmicomConfigureNormalBootImages)
    if(NOT UMICOM_BUILD_SYSTEM_IMAGES)
        return()
    endif()
    # Snapshot once before adding reverse dependencies. Each clone sees every
    # additive module and original source-local feature definition. It cannot
    # acquire an accidental dependency on itself through a later clone.
    get_target_property(UMICOM_STARTUP_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_STARTUP_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_STARTUP_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_STARTUP_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_STARTUP_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_STARTUP_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_STARTUP_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    set(UMICOM_STARTUP_PROFILES system recovery)
    if(BUILD_TESTING)
        list(APPEND UMICOM_STARTUP_PROFILES startup-check recovery-check forced-recovery-check)
    endif()
    foreach(UMICOM_STARTUP_PROFILE IN LISTS UMICOM_STARTUP_PROFILES)
        set(UMICOM_STARTUP_TARGET "umicom-${UMICOM_STARTUP_PROFILE}-image")
        add_executable(${UMICOM_STARTUP_TARGET} ${UMICOM_STARTUP_SOURCES})
        target_include_directories(${UMICOM_STARTUP_TARGET} PRIVATE ${UMICOM_STARTUP_INCLUDES})
        target_compile_options(${UMICOM_STARTUP_TARGET} PRIVATE ${UMICOM_STARTUP_OPTIONS})
        target_compile_definitions(${UMICOM_STARTUP_TARGET} PRIVATE UMICOM_KERNEL_NORMAL_BOOT=1)
        if(UMICOM_STARTUP_DEFINITIONS)
            target_compile_definitions(${UMICOM_STARTUP_TARGET} PRIVATE ${UMICOM_STARTUP_DEFINITIONS})
        endif()
        if(UMICOM_STARTUP_LIBRARIES)
            target_link_libraries(${UMICOM_STARTUP_TARGET} PRIVATE ${UMICOM_STARTUP_LIBRARIES})
        endif()
        target_link_options(${UMICOM_STARTUP_TARGET} PRIVATE ${UMICOM_STARTUP_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-${UMICOM_STARTUP_PROFILE}.map")
        set(UMICOM_STARTUP_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
        if(UMICOM_STARTUP_PROFILE STREQUAL "system" OR UMICOM_STARTUP_PROFILE STREQUAL "recovery")
            set(UMICOM_STARTUP_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
        else()
            target_compile_definitions(${UMICOM_STARTUP_TARGET} PRIVATE UMICOM_KERNEL_STARTUP_AUTOMATED=1)
        endif()
        if(UMICOM_STARTUP_PROFILE STREQUAL "recovery" OR UMICOM_STARTUP_PROFILE STREQUAL "forced-recovery-check")
            target_compile_definitions(${UMICOM_STARTUP_TARGET} PRIVATE UMICOM_KERNEL_FORCED_RECOVERY=1)
        endif()
        if(UMICOM_STARTUP_PROFILE STREQUAL "recovery-check")
            target_compile_definitions(${UMICOM_STARTUP_TARGET} PRIVATE UMICOM_KERNEL_STARTUP_FAILURE=1)
        endif()
        set_target_properties(${UMICOM_STARTUP_TARGET} PROPERTIES
            OUTPUT_NAME "umicom-${UMICOM_STARTUP_PROFILE}" SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${UMICOM_STARTUP_DIRECTORY}"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_STARTUP_DEPENDENCIES)
            add_dependencies(${UMICOM_STARTUP_TARGET} ${UMICOM_STARTUP_DEPENDENCIES})
        endif()
        # The existing build fixture now builds these images as well. A failed
        # compilation never authorises testing an older normal-startup image.
        add_dependencies(umicom-kernel ${UMICOM_STARTUP_TARGET})
    endforeach()
    if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
        foreach(UMICOM_STARTUP_TEST IN ITEMS startup-check recovery-check forced-recovery-check)
            add_test(NAME kernel.riscv64.${UMICOM_STARTUP_TEST}
                COMMAND "${UMICOM_QEMU_RISCV64}" -machine "virt,aclint=off"
                -bios "$<TARGET_FILE:umicom-${UMICOM_STARTUP_TEST}-image>"
                -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot)
            if(UMICOM_STARTUP_TEST STREQUAL "startup-check")
                set(UMICOM_STARTUP_MARKER "UMICOM_KERNEL_NORMAL_STARTUP_READY")
            else()
                set(UMICOM_STARTUP_MARKER "UMICOM_KERNEL_RECOVERY_SELECTION_READY")
            endif()
            set_tests_properties(kernel.riscv64.${UMICOM_STARTUP_TEST} PROPERTIES
                TIMEOUT 30 FIXTURES_REQUIRED kernel_current_image
                PASS_REGULAR_EXPRESSION "${UMICOM_STARTUP_MARKER}"
                FAIL_REGULAR_EXPRESSION "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC")
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL UmicomConfigureNormalBootImages)
