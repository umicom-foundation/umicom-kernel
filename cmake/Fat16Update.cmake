# Umicom Kernel bounded FAT16 existing-file data updates and persistence checks.
# Each write qualification uses a disposable copy of the established synthetic
# FAT16 disk. QEMU writeback caching honours explicit guest flushes; snapshot
# mode would suppress that contract and is deliberately absent here.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

# Reuse this module as the narrow CTest process gate. PASS_REGULAR_EXPRESSION
# can mask a child's nonzero exit, so the runner checks the actual QEMU result
# as well as complete readiness/end lines before returning success to CTest.
if(CMAKE_SCRIPT_MODE_FILE)
    foreach(UMICOM_FAT_RUN_PATH IN ITEMS UMICOM_FAT_UPDATE_QEMU UMICOM_FAT_UPDATE_IMAGE UMICOM_FAT_UPDATE_DISK)
        if(NOT DEFINED ${UMICOM_FAT_RUN_PATH} OR NOT EXISTS "${${UMICOM_FAT_RUN_PATH}}")
            message(FATAL_ERROR "The FAT16 guest runner requires an existing ${UMICOM_FAT_RUN_PATH} path.")
        endif()
    endforeach()
    if(UMICOM_FAT_UPDATE_DISK MATCHES ",")
        message(FATAL_ERROR "The FAT16 guest disk path contains a QEMU -drive separator comma.")
    endif()
    if(UMICOM_FAT_UPDATE_ROLE STREQUAL "writer")
        set(UMICOM_FAT_RUN_MARKER "UMICOM_KERNEL_FAT16_UPDATE_READY")
        set(UMICOM_FAT_RUN_DRIVE
            "file=${UMICOM_FAT_UPDATE_DISK},if=none,format=raw,id=umicom_fat_update,readonly=off,cache=writeback,rerror=report,werror=report")
        set(UMICOM_FAT_RUN_DEVICE
            "virtio-blk-device,drive=umicom_fat_update,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    elseif(UMICOM_FAT_UPDATE_ROLE STREQUAL "readback")
        set(UMICOM_FAT_RUN_MARKER "UMICOM_KERNEL_FAT16_UPDATE_READBACK_READY")
        set(UMICOM_FAT_RUN_DRIVE
            "file=${UMICOM_FAT_UPDATE_DISK},if=none,format=raw,id=umicom_fat_update_readback,readonly=on,cache=writeback,rerror=report")
        set(UMICOM_FAT_RUN_DEVICE
            "virtio-blk-device,drive=umicom_fat_update_readback,logical_block_size=512,physical_block_size=512,num-queues=1")
    else()
        message(FATAL_ERROR "The FAT16 guest runner role must be writer or readback.")
    endif()
    # The optional shorter limit permits independent runner-failure checks.
    # Registered guest tests use the default, inside their outer CTest limit.
    if(NOT DEFINED UMICOM_FAT_UPDATE_RUN_TIMEOUT)
        set(UMICOM_FAT_UPDATE_RUN_TIMEOUT 80)
    endif()
    if(NOT UMICOM_FAT_UPDATE_RUN_TIMEOUT MATCHES "^[1-9][0-9]*$" OR UMICOM_FAT_UPDATE_RUN_TIMEOUT GREATER 80)
        message(FATAL_ERROR "The FAT16 guest timeout must be an integer from 1 through 80 seconds.")
    endif()
    execute_process(COMMAND "${UMICOM_FAT_UPDATE_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_FAT_UPDATE_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "${UMICOM_FAT_RUN_DRIVE}" -device "${UMICOM_FAT_RUN_DEVICE}"
        RESULT_VARIABLE UMICOM_FAT_RUN_RESULT
        OUTPUT_VARIABLE UMICOM_FAT_RUN_OUTPUT ERROR_VARIABLE UMICOM_FAT_RUN_ERROR
        TIMEOUT "${UMICOM_FAT_UPDATE_RUN_TIMEOUT}")
    message("${UMICOM_FAT_RUN_OUTPUT}${UMICOM_FAT_RUN_ERROR}")
    if(NOT UMICOM_FAT_RUN_RESULT STREQUAL "0")
        message(FATAL_ERROR "The FAT16 ${UMICOM_FAT_UPDATE_ROLE} guest did not exit successfully: ${UMICOM_FAT_RUN_RESULT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_FAT_RUN_TEXT "${UMICOM_FAT_RUN_OUTPUT}${UMICOM_FAT_RUN_ERROR}")
    if(UMICOM_FAT_RUN_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "The FAT16 guest output contains a failure marker.")
    endif()
    string(FIND "\n${UMICOM_FAT_RUN_TEXT}\n" "\n${UMICOM_FAT_RUN_MARKER}\n" UMICOM_FAT_RUN_READY_POSITION)
    string(FIND "\n${UMICOM_FAT_RUN_TEXT}\n" "\nUMICOM_KERNEL_END\n" UMICOM_FAT_RUN_END_POSITION)
    if(UMICOM_FAT_RUN_READY_POSITION LESS 0 OR UMICOM_FAT_RUN_END_POSITION LESS UMICOM_FAT_RUN_READY_POSITION)
        message(FATAL_ERROR "The FAT16 guest did not emit its exact readiness line followed by the complete end line.")
    endif()
    return()
endif()

target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_update.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_update_console.c")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_FAT16_UPDATE=1)

function(UmicomConfigureFat16UpdateImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    get_target_property(UMICOM_FAT_UPDATE_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FAT_UPDATE_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FAT_UPDATE_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FAT_UPDATE_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FAT_UPDATE_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FAT_UPDATE_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FAT_UPDATE_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_FAT_UPDATE_ROLE IN ITEMS writer readback)
        if(UMICOM_FAT_UPDATE_ROLE STREQUAL "writer")
            set(UMICOM_FAT_UPDATE_TARGET umicom-fat16-update-image)
            set(UMICOM_FAT_UPDATE_OUTPUT umicom-fat16-update)
            set(UMICOM_FAT_UPDATE_DEFINITION UMICOM_KERNEL_FAT16_UPDATE_TEST=1)
        else()
            set(UMICOM_FAT_UPDATE_TARGET umicom-fat16-update-readback-image)
            set(UMICOM_FAT_UPDATE_OUTPUT umicom-fat16-update-readback)
            set(UMICOM_FAT_UPDATE_DEFINITION UMICOM_KERNEL_FAT16_UPDATE_READBACK_TEST=1)
        endif()
        add_executable(${UMICOM_FAT_UPDATE_TARGET} ${UMICOM_FAT_UPDATE_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_update_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_update_validation.c")
        target_include_directories(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_INCLUDES})
        target_compile_options(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_OPTIONS})
        target_compile_definitions(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_DEFINITION})
        if(UMICOM_FAT_UPDATE_DEFINITIONS)
            target_compile_definitions(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_DEFINITIONS})
        endif()
        if(UMICOM_FAT_UPDATE_LIBRARIES)
            target_link_libraries(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_LIBRARIES})
        endif()
        target_link_options(${UMICOM_FAT_UPDATE_TARGET} PRIVATE ${UMICOM_FAT_UPDATE_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_FAT_UPDATE_OUTPUT}.map")
        set_target_properties(${UMICOM_FAT_UPDATE_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_FAT_UPDATE_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_FAT_UPDATE_DEPENDENCIES)
            add_dependencies(${UMICOM_FAT_UPDATE_TARGET} ${UMICOM_FAT_UPDATE_DEPENDENCIES})
        endif()
    endforeach()
    # Add reverse dependencies after both snapshots, avoiding a clone cycle.
    add_dependencies(umicom-kernel umicom-fat16-update-image umicom-fat16-update-readback-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    set(UMICOM_FAT_UPDATE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-update.raw")
    if(UMICOM_FAT_UPDATE_FIXTURE MATCHES ",")
        message(FATAL_ERROR "The FAT16 update test path must not contain a QEMU -drive separator comma.")
    endif()
    add_test(NAME kernel.riscv64.fat16_update_prepare
        COMMAND "${CMAKE_COMMAND}" -E copy "${UMICOM_DISK_INSPECTION_FIXTURE_SOURCE}" "${UMICOM_FAT_UPDATE_FIXTURE}")
    set_tests_properties(kernel.riscv64.fat16_update_prepare PROPERTIES TIMEOUT 10
        FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP kernel_fat16_update_fixture)
    add_test(NAME kernel.riscv64.fat16_update
        COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_UPDATE_QEMU=${UMICOM_QEMU_RISCV64}"
            "-DUMICOM_FAT_UPDATE_IMAGE=$<TARGET_FILE:umicom-fat16-update-image>"
            "-DUMICOM_FAT_UPDATE_DISK=${UMICOM_FAT_UPDATE_FIXTURE}"
            "-DUMICOM_FAT_UPDATE_ROLE=writer"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Update.cmake")
    set_tests_properties(kernel.riscv64.fat16_update PROPERTIES TIMEOUT 90
        FIXTURES_REQUIRED "kernel_current_image;kernel_fat16_update_fixture"
        FIXTURES_SETUP kernel_fat16_update_completed)
    add_test(NAME kernel.riscv64.fat16_update_readback
        COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_UPDATE_QEMU=${UMICOM_QEMU_RISCV64}"
            "-DUMICOM_FAT_UPDATE_IMAGE=$<TARGET_FILE:umicom-fat16-update-readback-image>"
            "-DUMICOM_FAT_UPDATE_DISK=${UMICOM_FAT_UPDATE_FIXTURE}"
            "-DUMICOM_FAT_UPDATE_ROLE=readback"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Update.cmake")
    set_tests_properties(kernel.riscv64.fat16_update_readback PROPERTIES TIMEOUT 90
        FIXTURES_REQUIRED "kernel_current_image;kernel_fat16_update_fixture;kernel_fat16_update_completed")
    add_test(NAME kernel.riscv64.fat16_update_cleanup
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${UMICOM_FAT_UPDATE_FIXTURE}")
    # Cleanup has no image dependency: failed current builds must still remove
    # stale writable copies even when prepare and both guests are skipped.
    set_tests_properties(kernel.riscv64.fat16_update_cleanup PROPERTIES TIMEOUT 10
        FIXTURES_CLEANUP kernel_fat16_update_fixture)
endfunction()
cmake_language(DEFER CALL UmicomConfigureFat16UpdateImages)
