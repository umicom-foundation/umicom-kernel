# Umicom Kernel bounded FAT16 commit ordering and persistent interruption checks.
# Each writer receives a disposable copy of the established synthetic FAT16
# source. Stage and Finish use real writeback/FLUSH semantics without snapshots.
# Independent read-only processes accept clean completion or reject dirty media.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

# Keep the process gate in this build module rather than adding another script.
# Successful marker text alone cannot override a child error, signal or timeout.
if(CMAKE_SCRIPT_MODE_FILE)
    foreach(UMICOM_FAT_COMMIT_PATH IN ITEMS UMICOM_FAT_COMMIT_QEMU UMICOM_FAT_COMMIT_IMAGE UMICOM_FAT_COMMIT_DISK)
        if(NOT DEFINED ${UMICOM_FAT_COMMIT_PATH} OR NOT EXISTS "${${UMICOM_FAT_COMMIT_PATH}}")
            message(FATAL_ERROR "The FAT16 commit runner requires an existing ${UMICOM_FAT_COMMIT_PATH} path.")
        endif()
    endforeach()
    if(UMICOM_FAT_COMMIT_DISK MATCHES ",")
        message(FATAL_ERROR "The FAT16 commit disk path contains a QEMU -drive separator comma.")
    endif()
    if(UMICOM_FAT_COMMIT_ROLE STREQUAL "writer")
        set(UMICOM_FAT_COMMIT_MARKER "UMICOM_KERNEL_FAT16_COMMIT_READY")
    elseif(UMICOM_FAT_COMMIT_ROLE STREQUAL "readback")
        set(UMICOM_FAT_COMMIT_MARKER "UMICOM_KERNEL_FAT16_COMMIT_READBACK_READY")
    elseif(UMICOM_FAT_COMMIT_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_COMMIT_MARKER "UMICOM_KERNEL_FAT16_COMMIT_INTERRUPTED_READY")
    elseif(UMICOM_FAT_COMMIT_ROLE STREQUAL "rejected")
        set(UMICOM_FAT_COMMIT_MARKER "UMICOM_KERNEL_FAT16_COMMIT_REJECTED_READY")
    else()
        message(FATAL_ERROR "The FAT16 commit runner role must be writer, readback, interrupted or rejected.")
    endif()
    if(UMICOM_FAT_COMMIT_ROLE STREQUAL "writer" OR UMICOM_FAT_COMMIT_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_COMMIT_DRIVE
            "file=${UMICOM_FAT_COMMIT_DISK},if=none,format=raw,id=umicom_fat_commit,readonly=off,cache=writeback,rerror=report,werror=report")
        set(UMICOM_FAT_COMMIT_DEVICE
            "virtio-blk-device,drive=umicom_fat_commit,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    else()
        set(UMICOM_FAT_COMMIT_DRIVE
            "file=${UMICOM_FAT_COMMIT_DISK},if=none,format=raw,id=umicom_fat_commit,readonly=on,cache=writeback,rerror=report")
        set(UMICOM_FAT_COMMIT_DEVICE
            "virtio-blk-device,drive=umicom_fat_commit,logical_block_size=512,physical_block_size=512,num-queues=1")
    endif()
    # A shorter explicit limit supports independent negative runner checks.
    # Ordinary CTest registrations retain an 80-second child / 90-second test bound.
    if(NOT DEFINED UMICOM_FAT_COMMIT_RUN_TIMEOUT)
        set(UMICOM_FAT_COMMIT_RUN_TIMEOUT 80)
    endif()
    if(NOT UMICOM_FAT_COMMIT_RUN_TIMEOUT MATCHES "^[1-9][0-9]*$" OR UMICOM_FAT_COMMIT_RUN_TIMEOUT GREATER 80)
        message(FATAL_ERROR "The FAT16 commit timeout must be an integer from 1 through 80 seconds.")
    endif()
    execute_process(COMMAND "${UMICOM_FAT_COMMIT_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_FAT_COMMIT_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "${UMICOM_FAT_COMMIT_DRIVE}" -device "${UMICOM_FAT_COMMIT_DEVICE}"
        RESULT_VARIABLE UMICOM_FAT_COMMIT_EXIT
        OUTPUT_VARIABLE UMICOM_FAT_COMMIT_OUTPUT ERROR_VARIABLE UMICOM_FAT_COMMIT_ERROR
        TIMEOUT "${UMICOM_FAT_COMMIT_RUN_TIMEOUT}")
    message("${UMICOM_FAT_COMMIT_OUTPUT}${UMICOM_FAT_COMMIT_ERROR}")
    if(NOT UMICOM_FAT_COMMIT_EXIT STREQUAL "0")
        message(FATAL_ERROR "The FAT16 ${UMICOM_FAT_COMMIT_ROLE} guest did not exit successfully: ${UMICOM_FAT_COMMIT_EXIT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_FAT_COMMIT_TEXT "${UMICOM_FAT_COMMIT_OUTPUT}${UMICOM_FAT_COMMIT_ERROR}")
    if(UMICOM_FAT_COMMIT_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "The FAT16 commit guest output contains a failure marker.")
    endif()
    string(FIND "\n${UMICOM_FAT_COMMIT_TEXT}\n" "\n${UMICOM_FAT_COMMIT_MARKER}\n" UMICOM_FAT_COMMIT_READY_POSITION)
    string(FIND "\n${UMICOM_FAT_COMMIT_TEXT}\n" "\nUMICOM_KERNEL_END\n" UMICOM_FAT_COMMIT_END_POSITION)
    if(UMICOM_FAT_COMMIT_READY_POSITION LESS 0 OR UMICOM_FAT_COMMIT_END_POSITION LESS UMICOM_FAT_COMMIT_READY_POSITION)
        message(FATAL_ERROR "The FAT16 commit guest did not emit its exact readiness line followed by the complete end line.")
    endif()
    return()
endif()

# The normal shell keeps its earlier data-only commands and adds the explicit
# Stage/Finish lifetime through the already shared console implementation.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_FAT16_COMMIT=1)

function(UmicomConfigureFat16CommitImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    # Snapshot the final Kernel source graph, including its shared updater and
    # firmware bridge. Each role changes only the dedicated entry selection.
    get_target_property(UMICOM_FAT_COMMIT_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FAT_COMMIT_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FAT_COMMIT_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FAT_COMMIT_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FAT_COMMIT_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FAT_COMMIT_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FAT_COMMIT_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_FAT_COMMIT_ROLE IN ITEMS writer readback interrupted rejected)
        if(UMICOM_FAT_COMMIT_ROLE STREQUAL "writer")
            set(UMICOM_FAT_COMMIT_OUTPUT umicom-fat16-commit)
            set(UMICOM_FAT_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_COMMIT_TEST=1)
        elseif(UMICOM_FAT_COMMIT_ROLE STREQUAL "readback")
            set(UMICOM_FAT_COMMIT_OUTPUT umicom-fat16-commit-readback)
            set(UMICOM_FAT_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_COMMIT_READBACK_TEST=1)
        elseif(UMICOM_FAT_COMMIT_ROLE STREQUAL "interrupted")
            set(UMICOM_FAT_COMMIT_OUTPUT umicom-fat16-commit-interrupted)
            set(UMICOM_FAT_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_COMMIT_INTERRUPTED_TEST=1)
        else()
            set(UMICOM_FAT_COMMIT_OUTPUT umicom-fat16-commit-rejected)
            set(UMICOM_FAT_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_COMMIT_REJECTED_TEST=1)
        endif()
        set(UMICOM_FAT_COMMIT_TARGET "${UMICOM_FAT_COMMIT_OUTPUT}-image")
        add_executable(${UMICOM_FAT_COMMIT_TARGET} ${UMICOM_FAT_COMMIT_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_commit_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_commit_validation.c")
        target_include_directories(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_INCLUDES})
        target_compile_options(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_OPTIONS})
        target_compile_definitions(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_DEFINITION})
        if(UMICOM_FAT_COMMIT_DEFINITIONS)
            target_compile_definitions(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_DEFINITIONS})
        endif()
        if(UMICOM_FAT_COMMIT_LIBRARIES)
            target_link_libraries(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_LIBRARIES})
        endif()
        target_link_options(${UMICOM_FAT_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_COMMIT_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_FAT_COMMIT_OUTPUT}.map")
        set_target_properties(${UMICOM_FAT_COMMIT_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_FAT_COMMIT_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_FAT_COMMIT_DEPENDENCIES)
            add_dependencies(${UMICOM_FAT_COMMIT_TARGET} ${UMICOM_FAT_COMMIT_DEPENDENCIES})
        endif()
    endforeach()
    # Add reverse dependencies after all snapshots so clones cannot depend on
    # themselves. kernel_current_image rebuilds every acceptance executable.
    add_dependencies(umicom-kernel umicom-fat16-commit-image umicom-fat16-commit-readback-image
        umicom-fat16-commit-interrupted-image umicom-fat16-commit-rejected-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    foreach(UMICOM_FAT_COMMIT_SCENARIO IN ITEMS complete interrupted)
        if(UMICOM_FAT_COMMIT_SCENARIO STREQUAL "complete")
            set(UMICOM_FAT_COMMIT_BASE kernel.riscv64.fat16_commit)
            set(UMICOM_FAT_COMMIT_READER kernel.riscv64.fat16_commit_readback)
            set(UMICOM_FAT_COMMIT_WRITER_TARGET umicom-fat16-commit-image)
            set(UMICOM_FAT_COMMIT_READER_TARGET umicom-fat16-commit-readback-image)
            set(UMICOM_FAT_COMMIT_WRITER_ROLE writer)
            set(UMICOM_FAT_COMMIT_READER_ROLE readback)
            set(UMICOM_FAT_COMMIT_FIXTURE_ID kernel_fat16_commit_fixture)
            set(UMICOM_FAT_COMMIT_COMPLETE_ID kernel_fat16_commit_completed)
            set(UMICOM_FAT_COMMIT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-commit.raw")
        else()
            set(UMICOM_FAT_COMMIT_BASE kernel.riscv64.fat16_commit_interrupted)
            set(UMICOM_FAT_COMMIT_READER kernel.riscv64.fat16_commit_rejected)
            set(UMICOM_FAT_COMMIT_WRITER_TARGET umicom-fat16-commit-interrupted-image)
            set(UMICOM_FAT_COMMIT_READER_TARGET umicom-fat16-commit-rejected-image)
            set(UMICOM_FAT_COMMIT_WRITER_ROLE interrupted)
            set(UMICOM_FAT_COMMIT_READER_ROLE rejected)
            set(UMICOM_FAT_COMMIT_FIXTURE_ID kernel_fat16_commit_interrupted_fixture)
            set(UMICOM_FAT_COMMIT_COMPLETE_ID kernel_fat16_commit_interrupted_completed)
            set(UMICOM_FAT_COMMIT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-commit-interrupted.raw")
        endif()
        if(UMICOM_FAT_COMMIT_FIXTURE MATCHES ",")
            message(FATAL_ERROR "The FAT16 commit test path must not contain a QEMU -drive separator comma.")
        endif()
        add_test(NAME "${UMICOM_FAT_COMMIT_BASE}_prepare"
            COMMAND "${CMAKE_COMMAND}" -E copy "${UMICOM_DISK_INSPECTION_FIXTURE_SOURCE}" "${UMICOM_FAT_COMMIT_FIXTURE}")
        set_tests_properties("${UMICOM_FAT_COMMIT_BASE}_prepare" PROPERTIES TIMEOUT 10
            FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP "${UMICOM_FAT_COMMIT_FIXTURE_ID}")
        add_test(NAME "${UMICOM_FAT_COMMIT_BASE}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_COMMIT_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_COMMIT_IMAGE=$<TARGET_FILE:${UMICOM_FAT_COMMIT_WRITER_TARGET}>"
                "-DUMICOM_FAT_COMMIT_DISK=${UMICOM_FAT_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_COMMIT_ROLE=${UMICOM_FAT_COMMIT_WRITER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Commit.cmake")
        set_tests_properties("${UMICOM_FAT_COMMIT_BASE}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_COMMIT_FIXTURE_ID}"
            FIXTURES_SETUP "${UMICOM_FAT_COMMIT_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_COMMIT_READER}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_COMMIT_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_COMMIT_IMAGE=$<TARGET_FILE:${UMICOM_FAT_COMMIT_READER_TARGET}>"
                "-DUMICOM_FAT_COMMIT_DISK=${UMICOM_FAT_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_COMMIT_ROLE=${UMICOM_FAT_COMMIT_READER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Commit.cmake")
        set_tests_properties("${UMICOM_FAT_COMMIT_READER}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_COMMIT_FIXTURE_ID};${UMICOM_FAT_COMMIT_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_COMMIT_BASE}_cleanup"
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${UMICOM_FAT_COMMIT_FIXTURE}")
        # Cleanup has no successful-build prerequisite. It must still remove
        # stale writable copies when a current build prevents prepare/guests.
        set_tests_properties("${UMICOM_FAT_COMMIT_BASE}_cleanup" PROPERTIES TIMEOUT 10
            FIXTURES_CLEANUP "${UMICOM_FAT_COMMIT_FIXTURE_ID}")
    endforeach()
endfunction()
cmake_language(DEFER CALL UmicomConfigureFat16CommitImages)
