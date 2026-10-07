# Umicom Kernel bounded FAT16 data and directory-entry commit qualification.
# The checked synthetic source has FRAG.BIN's ARCHIVE bit initially clear.
# Each writer receives its own disposable copy. Explicit timestamps, archive
# policy and all neighbouring bytes are checked after real writeback/FLUSH
# operations in four independent guests, without snapshot mode.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

# Keep the process gate in this build module rather than adding another script.
# Successful marker text alone cannot override a child error, signal or timeout.
if(CMAKE_SCRIPT_MODE_FILE)
    # Cleanup is independent of build success and checks the actual path after
    # removal. A success marker never substitutes for this filesystem result.
    if(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "cleanup")
        if(NOT DEFINED UMICOM_FAT_FILE_COMMIT_DISK OR UMICOM_FAT_FILE_COMMIT_DISK STREQUAL "")
            message(FATAL_ERROR "The FAT16 file-commit cleanup requires its disposable disk path.")
        endif()
        file(REMOVE "${UMICOM_FAT_FILE_COMMIT_DISK}")
        if(EXISTS "${UMICOM_FAT_FILE_COMMIT_DISK}")
            message(FATAL_ERROR "The FAT16 file-commit disposable copy remains after cleanup.")
        endif()
        message("fat16-file-commit.cleanup=absent")
        return()
    endif()
    if(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "prepare")
        if(NOT DEFINED UMICOM_FAT_FILE_COMMIT_SOURCE OR NOT EXISTS "${UMICOM_FAT_FILE_COMMIT_SOURCE}" OR
           NOT DEFINED UMICOM_FAT_FILE_COMMIT_DISK OR UMICOM_FAT_FILE_COMMIT_DISK STREQUAL "" OR
           UMICOM_FAT_FILE_COMMIT_SOURCE STREQUAL UMICOM_FAT_FILE_COMMIT_DISK)
            message(FATAL_ERROR "The FAT16 file-commit preparation requires distinct source and disposable paths.")
        endif()
        file(SHA256 "${UMICOM_FAT_FILE_COMMIT_SOURCE}" UMICOM_FAT_FILE_COMMIT_SOURCE_HASH)
        if(NOT UMICOM_FAT_FILE_COMMIT_SOURCE_HASH STREQUAL "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
            message(FATAL_ERROR "The FAT16 file-commit synthetic source checksum changed.")
        endif()
        configure_file("${UMICOM_FAT_FILE_COMMIT_SOURCE}" "${UMICOM_FAT_FILE_COMMIT_DISK}" COPYONLY)
        file(SHA256 "${UMICOM_FAT_FILE_COMMIT_DISK}" UMICOM_FAT_FILE_COMMIT_COPY_HASH)
        if(NOT UMICOM_FAT_FILE_COMMIT_COPY_HASH STREQUAL UMICOM_FAT_FILE_COMMIT_SOURCE_HASH)
            message(FATAL_ERROR "The FAT16 file-commit disposable copy does not match its checked source.")
        endif()
        message("fat16-file-commit.prepare=checked-archive-clear-copy")
        return()
    endif()
    foreach(UMICOM_FAT_FILE_COMMIT_PATH IN ITEMS UMICOM_FAT_FILE_COMMIT_QEMU UMICOM_FAT_FILE_COMMIT_IMAGE UMICOM_FAT_FILE_COMMIT_DISK)
        if(NOT DEFINED ${UMICOM_FAT_FILE_COMMIT_PATH} OR NOT EXISTS "${${UMICOM_FAT_FILE_COMMIT_PATH}}")
            message(FATAL_ERROR "The FAT16 commit runner requires an existing ${UMICOM_FAT_FILE_COMMIT_PATH} path.")
        endif()
    endforeach()
    if(UMICOM_FAT_FILE_COMMIT_DISK MATCHES ",")
        message(FATAL_ERROR "The FAT16 commit disk path contains a QEMU -drive separator comma.")
    endif()
    if(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "writer")
        set(UMICOM_FAT_FILE_COMMIT_MARKER "UMICOM_KERNEL_FAT16_FILE_COMMIT_READY")
    elseif(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "readback")
        set(UMICOM_FAT_FILE_COMMIT_MARKER "UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_READY")
    elseif(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_FILE_COMMIT_MARKER "UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_READY")
    elseif(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "rejected")
        set(UMICOM_FAT_FILE_COMMIT_MARKER "UMICOM_KERNEL_FAT16_FILE_COMMIT_REJECTED_READY")
    else()
        message(FATAL_ERROR "The FAT16 commit runner role must be writer, readback, interrupted or rejected.")
    endif()
    if(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "writer" OR UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_FILE_COMMIT_DRIVE
            "file=${UMICOM_FAT_FILE_COMMIT_DISK},if=none,format=raw,id=umicom_fat_file_commit,readonly=off,cache=writeback,rerror=report,werror=report")
        set(UMICOM_FAT_FILE_COMMIT_DEVICE
            "virtio-blk-device,drive=umicom_fat_file_commit,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    else()
        set(UMICOM_FAT_FILE_COMMIT_DRIVE
            "file=${UMICOM_FAT_FILE_COMMIT_DISK},if=none,format=raw,id=umicom_fat_file_commit,readonly=on,cache=writeback,rerror=report")
        set(UMICOM_FAT_FILE_COMMIT_DEVICE
            "virtio-blk-device,drive=umicom_fat_file_commit,logical_block_size=512,physical_block_size=512,num-queues=1")
    endif()
    # A shorter explicit limit supports independent negative runner checks.
    # Ordinary CTest registrations retain an 80-second child / 90-second test bound.
    if(NOT DEFINED UMICOM_FAT_FILE_COMMIT_RUN_TIMEOUT)
        set(UMICOM_FAT_FILE_COMMIT_RUN_TIMEOUT 80)
    endif()
    if(NOT UMICOM_FAT_FILE_COMMIT_RUN_TIMEOUT MATCHES "^[1-9][0-9]*$" OR UMICOM_FAT_FILE_COMMIT_RUN_TIMEOUT GREATER 80)
        message(FATAL_ERROR "The FAT16 commit timeout must be an integer from 1 through 80 seconds.")
    endif()
    execute_process(COMMAND "${UMICOM_FAT_FILE_COMMIT_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_FAT_FILE_COMMIT_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "${UMICOM_FAT_FILE_COMMIT_DRIVE}" -device "${UMICOM_FAT_FILE_COMMIT_DEVICE}"
        RESULT_VARIABLE UMICOM_FAT_FILE_COMMIT_EXIT
        OUTPUT_VARIABLE UMICOM_FAT_FILE_COMMIT_OUTPUT ERROR_VARIABLE UMICOM_FAT_FILE_COMMIT_ERROR
        TIMEOUT "${UMICOM_FAT_FILE_COMMIT_RUN_TIMEOUT}")
    message("${UMICOM_FAT_FILE_COMMIT_OUTPUT}${UMICOM_FAT_FILE_COMMIT_ERROR}")
    if(NOT UMICOM_FAT_FILE_COMMIT_EXIT STREQUAL "0")
        message(FATAL_ERROR "The FAT16 ${UMICOM_FAT_FILE_COMMIT_ROLE} guest did not exit successfully: ${UMICOM_FAT_FILE_COMMIT_EXIT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_FAT_FILE_COMMIT_TEXT "${UMICOM_FAT_FILE_COMMIT_OUTPUT}${UMICOM_FAT_FILE_COMMIT_ERROR}")
    if(UMICOM_FAT_FILE_COMMIT_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "The FAT16 commit guest output contains a failure marker.")
    endif()
    string(FIND "\n${UMICOM_FAT_FILE_COMMIT_TEXT}\n" "\n${UMICOM_FAT_FILE_COMMIT_MARKER}\n" UMICOM_FAT_FILE_COMMIT_READY_POSITION)
    string(FIND "\n${UMICOM_FAT_FILE_COMMIT_TEXT}\n" "\nUMICOM_KERNEL_END\n" UMICOM_FAT_FILE_COMMIT_END_POSITION)
    if(UMICOM_FAT_FILE_COMMIT_READY_POSITION LESS 0 OR UMICOM_FAT_FILE_COMMIT_END_POSITION LESS UMICOM_FAT_FILE_COMMIT_READY_POSITION)
        message(FATAL_ERROR "The FAT16 commit guest did not emit its exact readiness line followed by the complete end line.")
    endif()
    return()
endif()

# The normal shell keeps its earlier data-only commands and adds a separate
# timestamped Stage/Finish owner through the shared console implementation.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_FAT16_FILE_COMMIT=1)

function(UmicomConfigureFat16FileCommitImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    # Snapshot the final Kernel source graph, including its shared updater and
    # firmware bridge. Each role changes only the dedicated entry selection.
    get_target_property(UMICOM_FAT_FILE_COMMIT_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FAT_FILE_COMMIT_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FAT_FILE_COMMIT_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FAT_FILE_COMMIT_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FAT_FILE_COMMIT_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FAT_FILE_COMMIT_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FAT_FILE_COMMIT_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_FAT_FILE_COMMIT_ROLE IN ITEMS writer readback interrupted rejected)
        if(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "writer")
            set(UMICOM_FAT_FILE_COMMIT_OUTPUT umicom-fat16-file-commit)
            set(UMICOM_FAT_FILE_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST=1)
        elseif(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "readback")
            set(UMICOM_FAT_FILE_COMMIT_OUTPUT umicom-fat16-file-commit-readback)
            set(UMICOM_FAT_FILE_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST=1)
        elseif(UMICOM_FAT_FILE_COMMIT_ROLE STREQUAL "interrupted")
            set(UMICOM_FAT_FILE_COMMIT_OUTPUT umicom-fat16-file-commit-interrupted)
            set(UMICOM_FAT_FILE_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST=1)
        else()
            set(UMICOM_FAT_FILE_COMMIT_OUTPUT umicom-fat16-file-commit-rejected)
            set(UMICOM_FAT_FILE_COMMIT_DEFINITION UMICOM_KERNEL_FAT16_FILE_COMMIT_REJECTED_TEST=1)
        endif()
        set(UMICOM_FAT_FILE_COMMIT_TARGET "${UMICOM_FAT_FILE_COMMIT_OUTPUT}-image")
        add_executable(${UMICOM_FAT_FILE_COMMIT_TARGET} ${UMICOM_FAT_FILE_COMMIT_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_file_commit_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_file_commit_validation.c")
        target_include_directories(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_INCLUDES})
        target_compile_options(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_OPTIONS})
        target_compile_definitions(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_DEFINITION})
        if(UMICOM_FAT_FILE_COMMIT_DEFINITIONS)
            target_compile_definitions(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_DEFINITIONS})
        endif()
        if(UMICOM_FAT_FILE_COMMIT_LIBRARIES)
            target_link_libraries(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_LIBRARIES})
        endif()
        target_link_options(${UMICOM_FAT_FILE_COMMIT_TARGET} PRIVATE ${UMICOM_FAT_FILE_COMMIT_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_FAT_FILE_COMMIT_OUTPUT}.map")
        set_target_properties(${UMICOM_FAT_FILE_COMMIT_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_FAT_FILE_COMMIT_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_FAT_FILE_COMMIT_DEPENDENCIES)
            add_dependencies(${UMICOM_FAT_FILE_COMMIT_TARGET} ${UMICOM_FAT_FILE_COMMIT_DEPENDENCIES})
        endif()
    endforeach()
    # Add reverse dependencies after all snapshots so clones cannot depend on
    # themselves. kernel_current_image rebuilds every acceptance executable.
    add_dependencies(umicom-kernel umicom-fat16-file-commit-image umicom-fat16-file-commit-readback-image
        umicom-fat16-file-commit-interrupted-image umicom-fat16-file-commit-rejected-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    set(UMICOM_FAT_FILE_COMMIT_FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fat16_file_commit/fixture.raw")
    file(SHA256 "${UMICOM_FAT_FILE_COMMIT_FIXTURE_SOURCE}" UMICOM_FAT_FILE_COMMIT_FIXTURE_HASH)
    if(NOT UMICOM_FAT_FILE_COMMIT_FIXTURE_HASH STREQUAL "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
        message(FATAL_ERROR "The synthetic archive-clear FAT16 fixture does not match its recorded SHA-256.")
    endif()
    foreach(UMICOM_FAT_FILE_COMMIT_SCENARIO IN ITEMS complete interrupted)
        if(UMICOM_FAT_FILE_COMMIT_SCENARIO STREQUAL "complete")
            set(UMICOM_FAT_FILE_COMMIT_BASE kernel.riscv64.fat16_file_commit)
            set(UMICOM_FAT_FILE_COMMIT_READER kernel.riscv64.fat16_file_commit_readback)
            set(UMICOM_FAT_FILE_COMMIT_WRITER_TARGET umicom-fat16-file-commit-image)
            set(UMICOM_FAT_FILE_COMMIT_READER_TARGET umicom-fat16-file-commit-readback-image)
            set(UMICOM_FAT_FILE_COMMIT_WRITER_ROLE writer)
            set(UMICOM_FAT_FILE_COMMIT_READER_ROLE readback)
            set(UMICOM_FAT_FILE_COMMIT_FIXTURE_ID kernel_fat16_file_commit_fixture)
            set(UMICOM_FAT_FILE_COMMIT_COMPLETE_ID kernel_fat16_file_commit_completed)
            set(UMICOM_FAT_FILE_COMMIT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-file-commit.raw")
        else()
            set(UMICOM_FAT_FILE_COMMIT_BASE kernel.riscv64.fat16_file_commit_interrupted)
            set(UMICOM_FAT_FILE_COMMIT_READER kernel.riscv64.fat16_file_commit_rejected)
            set(UMICOM_FAT_FILE_COMMIT_WRITER_TARGET umicom-fat16-file-commit-interrupted-image)
            set(UMICOM_FAT_FILE_COMMIT_READER_TARGET umicom-fat16-file-commit-rejected-image)
            set(UMICOM_FAT_FILE_COMMIT_WRITER_ROLE interrupted)
            set(UMICOM_FAT_FILE_COMMIT_READER_ROLE rejected)
            set(UMICOM_FAT_FILE_COMMIT_FIXTURE_ID kernel_fat16_file_commit_interrupted_fixture)
            set(UMICOM_FAT_FILE_COMMIT_COMPLETE_ID kernel_fat16_file_commit_interrupted_completed)
            set(UMICOM_FAT_FILE_COMMIT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-file-commit-interrupted.raw")
        endif()
        if(UMICOM_FAT_FILE_COMMIT_FIXTURE MATCHES ",")
            message(FATAL_ERROR "The FAT16 commit test path must not contain a QEMU -drive separator comma.")
        endif()
        add_test(NAME "${UMICOM_FAT_FILE_COMMIT_BASE}_prepare"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_FILE_COMMIT_SOURCE=${UMICOM_FAT_FILE_COMMIT_FIXTURE_SOURCE}"
                "-DUMICOM_FAT_FILE_COMMIT_DISK=${UMICOM_FAT_FILE_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_FILE_COMMIT_ROLE=prepare"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16FileCommit.cmake")
        set_tests_properties("${UMICOM_FAT_FILE_COMMIT_BASE}_prepare" PROPERTIES TIMEOUT 10
            FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP "${UMICOM_FAT_FILE_COMMIT_FIXTURE_ID}")
        add_test(NAME "${UMICOM_FAT_FILE_COMMIT_BASE}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_FILE_COMMIT_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_FILE_COMMIT_IMAGE=$<TARGET_FILE:${UMICOM_FAT_FILE_COMMIT_WRITER_TARGET}>"
                "-DUMICOM_FAT_FILE_COMMIT_DISK=${UMICOM_FAT_FILE_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_FILE_COMMIT_ROLE=${UMICOM_FAT_FILE_COMMIT_WRITER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16FileCommit.cmake")
        set_tests_properties("${UMICOM_FAT_FILE_COMMIT_BASE}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_FILE_COMMIT_FIXTURE_ID}"
            FIXTURES_SETUP "${UMICOM_FAT_FILE_COMMIT_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_FILE_COMMIT_READER}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_FILE_COMMIT_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_FILE_COMMIT_IMAGE=$<TARGET_FILE:${UMICOM_FAT_FILE_COMMIT_READER_TARGET}>"
                "-DUMICOM_FAT_FILE_COMMIT_DISK=${UMICOM_FAT_FILE_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_FILE_COMMIT_ROLE=${UMICOM_FAT_FILE_COMMIT_READER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16FileCommit.cmake")
        set_tests_properties("${UMICOM_FAT_FILE_COMMIT_READER}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_FILE_COMMIT_FIXTURE_ID};${UMICOM_FAT_FILE_COMMIT_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_FILE_COMMIT_BASE}_cleanup"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_FILE_COMMIT_DISK=${UMICOM_FAT_FILE_COMMIT_FIXTURE}"
                "-DUMICOM_FAT_FILE_COMMIT_ROLE=cleanup"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16FileCommit.cmake")
        # Cleanup has no successful-build prerequisite. It must still remove
        # stale writable copies when a current build prevents prepare/guests.
        set_tests_properties("${UMICOM_FAT_FILE_COMMIT_BASE}_cleanup" PROPERTIES TIMEOUT 10
            FIXTURES_CLEANUP "${UMICOM_FAT_FILE_COMMIT_FIXTURE_ID}")
    endforeach()
endfunction()
cmake_language(DEFER CALL UmicomConfigureFat16FileCommitImages)
