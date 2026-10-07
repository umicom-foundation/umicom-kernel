# Umicom Kernel bounded FAT16 append and persisted-size qualification.
# The checked synthetic source has FRAG.BIN's ARCHIVE bit initially clear.
# Each writer receives its own disposable copy. Explicit timestamps, archive
# policy, increased EOF and all neighbouring bytes are checked after real writeback/FLUSH
# operations in four independent guests, without snapshot mode.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

# Keep the process gate in this build module rather than adding another script.
# Successful marker text alone cannot override a child error, signal or timeout.
if(CMAKE_SCRIPT_MODE_FILE)
    # This checked source is never a writable or removable disposable image.
    file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fat16_file_commit/fixture.raw" UMICOM_FAT_APPEND_CHECKED_SOURCE)
    if(DEFINED UMICOM_FAT_APPEND_DISK AND EXISTS "${UMICOM_FAT_APPEND_DISK}")
        file(REAL_PATH "${UMICOM_FAT_APPEND_DISK}" UMICOM_FAT_APPEND_REAL_DISK)
        if(UMICOM_FAT_APPEND_REAL_DISK STREQUAL UMICOM_FAT_APPEND_CHECKED_SOURCE)
            message(FATAL_ERROR "The checked source cannot be an append disposable image.")
        endif()
    endif()
    # Cleanup is independent of build success and checks the actual path after
    # removal. A success marker never substitutes for this filesystem result.
    if(UMICOM_FAT_APPEND_ROLE STREQUAL "cleanup")
        if(NOT DEFINED UMICOM_FAT_APPEND_DISK OR UMICOM_FAT_APPEND_DISK STREQUAL "")
            message(FATAL_ERROR "The FAT16 append cleanup requires its disposable disk path.")
        endif()
        file(REMOVE "${UMICOM_FAT_APPEND_DISK}")
        if(EXISTS "${UMICOM_FAT_APPEND_DISK}")
            message(FATAL_ERROR "The FAT16 append disposable copy remains after cleanup.")
        endif()
        message("fat16-append.cleanup=absent")
        return()
    endif()
    if(UMICOM_FAT_APPEND_ROLE STREQUAL "prepare")
        if(NOT DEFINED UMICOM_FAT_APPEND_SOURCE OR NOT EXISTS "${UMICOM_FAT_APPEND_SOURCE}" OR
           NOT DEFINED UMICOM_FAT_APPEND_DISK OR UMICOM_FAT_APPEND_DISK STREQUAL "" OR
           UMICOM_FAT_APPEND_SOURCE STREQUAL UMICOM_FAT_APPEND_DISK)
            message(FATAL_ERROR "The FAT16 append preparation requires distinct source and disposable paths.")
        endif()
        file(SHA256 "${UMICOM_FAT_APPEND_SOURCE}" UMICOM_FAT_APPEND_SOURCE_HASH)
        if(NOT UMICOM_FAT_APPEND_SOURCE_HASH STREQUAL "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
            message(FATAL_ERROR "The FAT16 append synthetic source checksum changed.")
        endif()
        configure_file("${UMICOM_FAT_APPEND_SOURCE}" "${UMICOM_FAT_APPEND_DISK}" COPYONLY)
        file(SHA256 "${UMICOM_FAT_APPEND_DISK}" UMICOM_FAT_APPEND_COPY_HASH)
        if(NOT UMICOM_FAT_APPEND_COPY_HASH STREQUAL UMICOM_FAT_APPEND_SOURCE_HASH)
            message(FATAL_ERROR "The FAT16 append disposable copy does not match its checked source.")
        endif()
        message("fat16-append.prepare=checked-archive-clear-copy")
        return()
    endif()
    foreach(UMICOM_FAT_APPEND_PATH IN ITEMS UMICOM_FAT_APPEND_QEMU UMICOM_FAT_APPEND_IMAGE UMICOM_FAT_APPEND_DISK)
        if(NOT DEFINED ${UMICOM_FAT_APPEND_PATH} OR NOT EXISTS "${${UMICOM_FAT_APPEND_PATH}}")
            message(FATAL_ERROR "The FAT16 append runner requires an existing ${UMICOM_FAT_APPEND_PATH} path.")
        endif()
    endforeach()
    if(UMICOM_FAT_APPEND_DISK MATCHES ",")
        message(FATAL_ERROR "The FAT16 append disk path contains a QEMU -drive separator comma.")
    endif()
    if(UMICOM_FAT_APPEND_ROLE STREQUAL "writer")
        set(UMICOM_FAT_APPEND_MARKER "UMICOM_KERNEL_FAT16_APPEND_READY")
        set(UMICOM_FAT_APPEND_BEFORE "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
        set(UMICOM_FAT_APPEND_AFTER "722494e794b62b831ee303713ffb32651a030060a5a08acca2ffef66907bb481")
    elseif(UMICOM_FAT_APPEND_ROLE STREQUAL "readback")
        set(UMICOM_FAT_APPEND_MARKER "UMICOM_KERNEL_FAT16_APPEND_READBACK_READY")
        set(UMICOM_FAT_APPEND_BEFORE "722494e794b62b831ee303713ffb32651a030060a5a08acca2ffef66907bb481")
        set(UMICOM_FAT_APPEND_AFTER "${UMICOM_FAT_APPEND_BEFORE}")
    elseif(UMICOM_FAT_APPEND_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_APPEND_MARKER "UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_READY")
        set(UMICOM_FAT_APPEND_BEFORE "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
        set(UMICOM_FAT_APPEND_AFTER "f784c7c11dc4d837fd5521956eaf2faaffa7ef0fdf7f1ff238b89764491f53a7")
    elseif(UMICOM_FAT_APPEND_ROLE STREQUAL "rejected")
        set(UMICOM_FAT_APPEND_MARKER "UMICOM_KERNEL_FAT16_APPEND_REJECTED_READY")
        set(UMICOM_FAT_APPEND_BEFORE "f784c7c11dc4d837fd5521956eaf2faaffa7ef0fdf7f1ff238b89764491f53a7")
        set(UMICOM_FAT_APPEND_AFTER "${UMICOM_FAT_APPEND_BEFORE}")
    else()
        message(FATAL_ERROR "The FAT16 append runner role must be writer, readback, interrupted or rejected.")
    endif()
    file(SHA256 "${UMICOM_FAT_APPEND_DISK}" UMICOM_FAT_APPEND_BEFORE_HASH)
    message("fat16-append.image-before=${UMICOM_FAT_APPEND_BEFORE_HASH}")
    if(NOT UMICOM_FAT_APPEND_BEFORE_HASH STREQUAL UMICOM_FAT_APPEND_BEFORE)
        message(FATAL_ERROR "The append role's whole input image does not match its exact fixture.")
    endif()
    if(UMICOM_FAT_APPEND_ROLE STREQUAL "writer" OR UMICOM_FAT_APPEND_ROLE STREQUAL "interrupted")
        set(UMICOM_FAT_APPEND_DRIVE
            "file=${UMICOM_FAT_APPEND_DISK},if=none,format=raw,id=umicom_fat_append,readonly=off,cache=writeback,rerror=report,werror=report")
        set(UMICOM_FAT_APPEND_DEVICE
            "virtio-blk-device,drive=umicom_fat_append,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    else()
        set(UMICOM_FAT_APPEND_DRIVE
            "file=${UMICOM_FAT_APPEND_DISK},if=none,format=raw,id=umicom_fat_append,readonly=on,cache=writeback,rerror=report")
        set(UMICOM_FAT_APPEND_DEVICE
            "virtio-blk-device,drive=umicom_fat_append,logical_block_size=512,physical_block_size=512,num-queues=1")
    endif()
    # A shorter explicit limit supports independent negative runner checks.
    # Ordinary CTest registrations retain an 80-second child / 90-second test bound.
    if(NOT DEFINED UMICOM_FAT_APPEND_RUN_TIMEOUT)
        set(UMICOM_FAT_APPEND_RUN_TIMEOUT 80)
    endif()
    if(NOT UMICOM_FAT_APPEND_RUN_TIMEOUT MATCHES "^[1-9][0-9]*$" OR UMICOM_FAT_APPEND_RUN_TIMEOUT GREATER 80)
        message(FATAL_ERROR "The FAT16 append timeout must be an integer from 1 through 80 seconds.")
    endif()
    execute_process(COMMAND "${UMICOM_FAT_APPEND_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_FAT_APPEND_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "${UMICOM_FAT_APPEND_DRIVE}" -device "${UMICOM_FAT_APPEND_DEVICE}"
        RESULT_VARIABLE UMICOM_FAT_APPEND_EXIT
        OUTPUT_VARIABLE UMICOM_FAT_APPEND_OUTPUT ERROR_VARIABLE UMICOM_FAT_APPEND_ERROR
        TIMEOUT "${UMICOM_FAT_APPEND_RUN_TIMEOUT}")
    message("${UMICOM_FAT_APPEND_OUTPUT}${UMICOM_FAT_APPEND_ERROR}")
    # Inspect actual backing bytes even when the child exits unsuccessfully.
    file(SHA256 "${UMICOM_FAT_APPEND_DISK}" UMICOM_FAT_APPEND_AFTER_HASH)
    message("fat16-append.image-after=${UMICOM_FAT_APPEND_AFTER_HASH}")
    if(NOT UMICOM_FAT_APPEND_AFTER_HASH STREQUAL UMICOM_FAT_APPEND_AFTER)
        message(FATAL_ERROR "The append role's whole output image does not match its exact fixture.")
    endif()
    if(UMICOM_FAT_APPEND_ROLE STREQUAL "readback" OR UMICOM_FAT_APPEND_ROLE STREQUAL "rejected")
        message("fat16-append.backing-image=unchanged")
    else()
        message("fat16-append.backing-image=exact-append-result")
    endif()
    if(NOT UMICOM_FAT_APPEND_EXIT STREQUAL "0")
        message(FATAL_ERROR "The FAT16 ${UMICOM_FAT_APPEND_ROLE} guest did not exit successfully: ${UMICOM_FAT_APPEND_EXIT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_FAT_APPEND_TEXT "${UMICOM_FAT_APPEND_OUTPUT}${UMICOM_FAT_APPEND_ERROR}")
    if(UMICOM_FAT_APPEND_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "The FAT16 append guest output contains a failure marker.")
    endif()
    string(FIND "\n${UMICOM_FAT_APPEND_TEXT}\n" "\n${UMICOM_FAT_APPEND_MARKER}\n" UMICOM_FAT_APPEND_READY_POSITION)
    string(FIND "\n${UMICOM_FAT_APPEND_TEXT}\n" "\nUMICOM_KERNEL_END\n" UMICOM_FAT_APPEND_END_POSITION)
    if(UMICOM_FAT_APPEND_READY_POSITION LESS 0 OR UMICOM_FAT_APPEND_END_POSITION LESS UMICOM_FAT_APPEND_READY_POSITION)
        message(FATAL_ERROR "The FAT16 append guest did not emit its exact readiness line followed by the complete end line.")
    endif()
    return()
endif()

function(UmicomConfigureFat16AppendImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    # Snapshot the final Kernel source graph, including its shared updater and
    # firmware bridge. Each role changes only the dedicated entry selection.
    get_target_property(UMICOM_FAT_APPEND_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FAT_APPEND_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FAT_APPEND_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FAT_APPEND_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FAT_APPEND_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FAT_APPEND_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FAT_APPEND_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_FAT_APPEND_ROLE IN ITEMS writer readback interrupted rejected)
        if(UMICOM_FAT_APPEND_ROLE STREQUAL "writer")
            set(UMICOM_FAT_APPEND_OUTPUT umicom-fat16-append)
            set(UMICOM_FAT_APPEND_DEFINITION UMICOM_KERNEL_FAT16_APPEND_TEST=1)
        elseif(UMICOM_FAT_APPEND_ROLE STREQUAL "readback")
            set(UMICOM_FAT_APPEND_OUTPUT umicom-fat16-append-readback)
            set(UMICOM_FAT_APPEND_DEFINITION UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST=1)
        elseif(UMICOM_FAT_APPEND_ROLE STREQUAL "interrupted")
            set(UMICOM_FAT_APPEND_OUTPUT umicom-fat16-append-interrupted)
            set(UMICOM_FAT_APPEND_DEFINITION UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST=1)
        else()
            set(UMICOM_FAT_APPEND_OUTPUT umicom-fat16-append-rejected)
            set(UMICOM_FAT_APPEND_DEFINITION UMICOM_KERNEL_FAT16_APPEND_REJECTED_TEST=1)
        endif()
        set(UMICOM_FAT_APPEND_TARGET "${UMICOM_FAT_APPEND_OUTPUT}-image")
        add_executable(${UMICOM_FAT_APPEND_TARGET} ${UMICOM_FAT_APPEND_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_append_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_append_validation.c")
        target_include_directories(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_INCLUDES})
        target_compile_options(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_OPTIONS})
        target_compile_definitions(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_DEFINITION})
        if(UMICOM_FAT_APPEND_DEFINITIONS)
            target_compile_definitions(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_DEFINITIONS})
        endif()
        if(UMICOM_FAT_APPEND_LIBRARIES)
            target_link_libraries(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_LIBRARIES})
        endif()
        target_link_options(${UMICOM_FAT_APPEND_TARGET} PRIVATE ${UMICOM_FAT_APPEND_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_FAT_APPEND_OUTPUT}.map")
        set_target_properties(${UMICOM_FAT_APPEND_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_FAT_APPEND_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_FAT_APPEND_DEPENDENCIES)
            add_dependencies(${UMICOM_FAT_APPEND_TARGET} ${UMICOM_FAT_APPEND_DEPENDENCIES})
        endif()
    endforeach()
    # Add reverse dependencies after all snapshots so clones cannot depend on
    # themselves. kernel_current_image rebuilds every acceptance executable.
    add_dependencies(umicom-kernel umicom-fat16-append-image umicom-fat16-append-readback-image
        umicom-fat16-append-interrupted-image umicom-fat16-append-rejected-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    set(UMICOM_FAT_APPEND_FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fat16_file_commit/fixture.raw")
    file(SHA256 "${UMICOM_FAT_APPEND_FIXTURE_SOURCE}" UMICOM_FAT_APPEND_FIXTURE_HASH)
    if(NOT UMICOM_FAT_APPEND_FIXTURE_HASH STREQUAL "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
        message(FATAL_ERROR "The synthetic archive-clear FAT16 fixture does not match its recorded SHA-256.")
    endif()
    foreach(UMICOM_FAT_APPEND_SCENARIO IN ITEMS complete interrupted)
        if(UMICOM_FAT_APPEND_SCENARIO STREQUAL "complete")
            set(UMICOM_FAT_APPEND_BASE kernel.riscv64.fat16_append)
            set(UMICOM_FAT_APPEND_READER kernel.riscv64.fat16_append_readback)
            set(UMICOM_FAT_APPEND_WRITER_TARGET umicom-fat16-append-image)
            set(UMICOM_FAT_APPEND_READER_TARGET umicom-fat16-append-readback-image)
            set(UMICOM_FAT_APPEND_WRITER_ROLE writer)
            set(UMICOM_FAT_APPEND_READER_ROLE readback)
            set(UMICOM_FAT_APPEND_FIXTURE_ID kernel_fat16_append_fixture)
            set(UMICOM_FAT_APPEND_COMPLETE_ID kernel_fat16_append_completed)
            set(UMICOM_FAT_APPEND_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-append.raw")
        else()
            set(UMICOM_FAT_APPEND_BASE kernel.riscv64.fat16_append_interrupted)
            set(UMICOM_FAT_APPEND_READER kernel.riscv64.fat16_append_rejected)
            set(UMICOM_FAT_APPEND_WRITER_TARGET umicom-fat16-append-interrupted-image)
            set(UMICOM_FAT_APPEND_READER_TARGET umicom-fat16-append-rejected-image)
            set(UMICOM_FAT_APPEND_WRITER_ROLE interrupted)
            set(UMICOM_FAT_APPEND_READER_ROLE rejected)
            set(UMICOM_FAT_APPEND_FIXTURE_ID kernel_fat16_append_interrupted_fixture)
            set(UMICOM_FAT_APPEND_COMPLETE_ID kernel_fat16_append_interrupted_completed)
            set(UMICOM_FAT_APPEND_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-append-interrupted.raw")
        endif()
        if(UMICOM_FAT_APPEND_FIXTURE MATCHES ",")
            message(FATAL_ERROR "The FAT16 append test path must not contain a QEMU -drive separator comma.")
        endif()
        add_test(NAME "${UMICOM_FAT_APPEND_BASE}_prepare"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_APPEND_SOURCE=${UMICOM_FAT_APPEND_FIXTURE_SOURCE}"
                "-DUMICOM_FAT_APPEND_DISK=${UMICOM_FAT_APPEND_FIXTURE}"
                "-DUMICOM_FAT_APPEND_ROLE=prepare"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Append.cmake")
        set_tests_properties("${UMICOM_FAT_APPEND_BASE}_prepare" PROPERTIES TIMEOUT 10
            FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP "${UMICOM_FAT_APPEND_FIXTURE_ID}")
        add_test(NAME "${UMICOM_FAT_APPEND_BASE}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_APPEND_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_APPEND_IMAGE=$<TARGET_FILE:${UMICOM_FAT_APPEND_WRITER_TARGET}>"
                "-DUMICOM_FAT_APPEND_DISK=${UMICOM_FAT_APPEND_FIXTURE}"
                "-DUMICOM_FAT_APPEND_ROLE=${UMICOM_FAT_APPEND_WRITER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Append.cmake")
        set_tests_properties("${UMICOM_FAT_APPEND_BASE}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_APPEND_FIXTURE_ID}"
            FIXTURES_SETUP "${UMICOM_FAT_APPEND_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_APPEND_READER}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_APPEND_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_APPEND_IMAGE=$<TARGET_FILE:${UMICOM_FAT_APPEND_READER_TARGET}>"
                "-DUMICOM_FAT_APPEND_DISK=${UMICOM_FAT_APPEND_FIXTURE}"
                "-DUMICOM_FAT_APPEND_ROLE=${UMICOM_FAT_APPEND_READER_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Append.cmake")
        set_tests_properties("${UMICOM_FAT_APPEND_READER}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "kernel_current_image;${UMICOM_FAT_APPEND_FIXTURE_ID};${UMICOM_FAT_APPEND_COMPLETE_ID}")
        add_test(NAME "${UMICOM_FAT_APPEND_BASE}_cleanup"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_APPEND_DISK=${UMICOM_FAT_APPEND_FIXTURE}"
                "-DUMICOM_FAT_APPEND_ROLE=cleanup"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Append.cmake")
        # Cleanup has no successful-build prerequisite. It must still remove
        # stale writable copies when a current build prevents prepare/guests.
        set_tests_properties("${UMICOM_FAT_APPEND_BASE}_cleanup" PROPERTIES TIMEOUT 10
            FIXTURES_CLEANUP "${UMICOM_FAT_APPEND_FIXTURE_ID}")
    endforeach()
endfunction()
cmake_language(DEFER CALL UmicomConfigureFat16AppendImages)
