# Umicom Kernel read-only persisted FAT16 metadata qualification.
# Initial metadata comes from the checked archive-clear source. Two additional
# readers join the existing completed/interrupted file-commit fixture lifetimes,
# so their original cleanup runs only after every reader has finished.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

if(CMAKE_SCRIPT_MODE_FILE)
    foreach(UMICOM_FAT_METADATA_PATH IN ITEMS UMICOM_FAT_METADATA_QEMU UMICOM_FAT_METADATA_IMAGE UMICOM_FAT_METADATA_DISK)
        if(NOT DEFINED ${UMICOM_FAT_METADATA_PATH} OR NOT EXISTS "${${UMICOM_FAT_METADATA_PATH}}")
            message(FATAL_ERROR "The FAT16 metadata runner requires an existing ${UMICOM_FAT_METADATA_PATH} path.")
        endif()
    endforeach()
    if(UMICOM_FAT_METADATA_DISK MATCHES ",")
        message(FATAL_ERROR "The FAT16 metadata disk path contains a QEMU -drive separator comma.")
    endif()
    if(UMICOM_FAT_METADATA_ROLE STREQUAL "initial")
        set(UMICOM_FAT_METADATA_MARKER "UMICOM_KERNEL_FAT16_METADATA_READY")
        set(UMICOM_FAT_METADATA_EXPECTED "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
    elseif(UMICOM_FAT_METADATA_ROLE STREQUAL "committed")
        set(UMICOM_FAT_METADATA_MARKER "UMICOM_KERNEL_FAT16_METADATA_COMMITTED_READY")
        set(UMICOM_FAT_METADATA_EXPECTED "093943b3c476951485d40e7cd7ad10d77c5323a291253ae0b6e46f10dbf411c2")
    elseif(UMICOM_FAT_METADATA_ROLE STREQUAL "dirty")
        set(UMICOM_FAT_METADATA_MARKER "UMICOM_KERNEL_FAT16_METADATA_DIRTY_READY")
        set(UMICOM_FAT_METADATA_EXPECTED "29984d78b60f7242cc430046057b3e9bc1aa7c4e880e94138ae49b0a4238c522")
    else()
        message(FATAL_ERROR "The FAT16 metadata runner role must be initial, committed or dirty.")
    endif()
    file(SHA256 "${UMICOM_FAT_METADATA_DISK}" UMICOM_FAT_METADATA_BEFORE)
    if(NOT UMICOM_FAT_METADATA_BEFORE STREQUAL UMICOM_FAT_METADATA_EXPECTED)
        message(FATAL_ERROR "The FAT16 metadata input does not match its complete expected fixture image.")
    endif()
    # A shorter limit supports process-gate negative checks; ordinary tests
    # retain an 80-second child limit within the 90-second CTest bound.
    if(NOT DEFINED UMICOM_FAT_METADATA_RUN_TIMEOUT)
        set(UMICOM_FAT_METADATA_RUN_TIMEOUT 80)
    endif()
    if(NOT UMICOM_FAT_METADATA_RUN_TIMEOUT MATCHES "^[1-9][0-9]*$" OR UMICOM_FAT_METADATA_RUN_TIMEOUT GREATER 80)
        message(FATAL_ERROR "The FAT16 metadata timeout must be an integer from 1 through 80 seconds.")
    endif()
    execute_process(COMMAND "${UMICOM_FAT_METADATA_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_FAT_METADATA_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "file=${UMICOM_FAT_METADATA_DISK},if=none,format=raw,id=umicom_fat_metadata,readonly=on,cache=writeback,rerror=report"
        -device "virtio-blk-device,drive=umicom_fat_metadata,logical_block_size=512,physical_block_size=512,num-queues=1"
        RESULT_VARIABLE UMICOM_FAT_METADATA_EXIT
        OUTPUT_VARIABLE UMICOM_FAT_METADATA_OUTPUT ERROR_VARIABLE UMICOM_FAT_METADATA_ERROR
        TIMEOUT "${UMICOM_FAT_METADATA_RUN_TIMEOUT}")
    message("${UMICOM_FAT_METADATA_OUTPUT}${UMICOM_FAT_METADATA_ERROR}")
    # Check every backing-file byte after the process, including an error exit.
    # Guest markers alone cannot establish either completion or non-mutation.
    file(SHA256 "${UMICOM_FAT_METADATA_DISK}" UMICOM_FAT_METADATA_AFTER)
    message("fat16-metadata.image-before=${UMICOM_FAT_METADATA_BEFORE}")
    message("fat16-metadata.image-after=${UMICOM_FAT_METADATA_AFTER}")
    if(NOT UMICOM_FAT_METADATA_AFTER STREQUAL UMICOM_FAT_METADATA_BEFORE)
        message(FATAL_ERROR "The read-only FAT16 metadata guest's backing image changed.")
    endif()
    if(NOT UMICOM_FAT_METADATA_EXIT STREQUAL "0")
        message(FATAL_ERROR "The FAT16 metadata guest did not exit successfully: ${UMICOM_FAT_METADATA_EXIT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_FAT_METADATA_TEXT "${UMICOM_FAT_METADATA_OUTPUT}${UMICOM_FAT_METADATA_ERROR}")
    if(UMICOM_FAT_METADATA_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "The FAT16 metadata guest output contains a failure marker.")
    endif()
    string(FIND "\n${UMICOM_FAT_METADATA_TEXT}\n" "\n${UMICOM_FAT_METADATA_MARKER}\n" UMICOM_FAT_METADATA_READY_POSITION)
    string(FIND "\n${UMICOM_FAT_METADATA_TEXT}\n" "\nUMICOM_KERNEL_END\n" UMICOM_FAT_METADATA_END_POSITION)
    if(UMICOM_FAT_METADATA_READY_POSITION LESS 0 OR UMICOM_FAT_METADATA_END_POSITION LESS UMICOM_FAT_METADATA_READY_POSITION)
        message(FATAL_ERROR "The FAT16 metadata guest requires its exact readiness line followed by the complete end line.")
    endif()
    message("fat16-metadata.backing-image=unchanged")
    return()
endif()

function(UmicomConfigureFat16MetadataImages)
    if(NOT BUILD_TESTING)
        return()
    endif()
    get_target_property(UMICOM_FAT_METADATA_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_FAT_METADATA_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_FAT_METADATA_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_FAT_METADATA_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_FAT_METADATA_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_FAT_METADATA_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_FAT_METADATA_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_FAT_METADATA_ROLE IN ITEMS initial committed dirty)
        if(UMICOM_FAT_METADATA_ROLE STREQUAL "initial")
            set(UMICOM_FAT_METADATA_OUTPUT umicom-fat16-metadata)
            set(UMICOM_FAT_METADATA_DEFINITION UMICOM_KERNEL_FAT16_METADATA_TEST=1)
        elseif(UMICOM_FAT_METADATA_ROLE STREQUAL "committed")
            set(UMICOM_FAT_METADATA_OUTPUT umicom-fat16-metadata-committed)
            set(UMICOM_FAT_METADATA_DEFINITION UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST=1)
        else()
            set(UMICOM_FAT_METADATA_OUTPUT umicom-fat16-metadata-dirty)
            set(UMICOM_FAT_METADATA_DEFINITION UMICOM_KERNEL_FAT16_METADATA_DIRTY_TEST=1)
        endif()
        set(UMICOM_FAT_METADATA_TARGET "${UMICOM_FAT_METADATA_OUTPUT}-image")
        add_executable(${UMICOM_FAT_METADATA_TARGET} ${UMICOM_FAT_METADATA_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_metadata_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_metadata_validation.c")
        target_include_directories(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_INCLUDES})
        target_compile_options(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_OPTIONS})
        target_compile_definitions(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_DEFINITION})
        if(UMICOM_FAT_METADATA_DEFINITIONS)
            target_compile_definitions(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_DEFINITIONS})
        endif()
        if(UMICOM_FAT_METADATA_LIBRARIES)
            target_link_libraries(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_LIBRARIES})
        endif()
        target_link_options(${UMICOM_FAT_METADATA_TARGET} PRIVATE ${UMICOM_FAT_METADATA_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_FAT_METADATA_OUTPUT}.map")
        set_target_properties(${UMICOM_FAT_METADATA_TARGET} PROPERTIES OUTPUT_NAME "${UMICOM_FAT_METADATA_OUTPUT}"
            SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        if(UMICOM_FAT_METADATA_DEPENDENCIES)
            add_dependencies(${UMICOM_FAT_METADATA_TARGET} ${UMICOM_FAT_METADATA_DEPENDENCIES})
        endif()
    endforeach()
    # All clones are complete before the ordinary current-image target depends
    # on them. Readers reuse producer fixtures rather than opening source media
    # writable or introducing independent copies with unrelated cleanup.
    add_dependencies(umicom-kernel umicom-fat16-metadata-image
        umicom-fat16-metadata-committed-image umicom-fat16-metadata-dirty-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    foreach(UMICOM_FAT_METADATA_ROLE IN ITEMS initial committed dirty)
        if(UMICOM_FAT_METADATA_ROLE STREQUAL "initial")
            set(UMICOM_FAT_METADATA_NAME kernel.riscv64.fat16_metadata)
            set(UMICOM_FAT_METADATA_TARGET umicom-fat16-metadata-image)
            set(UMICOM_FAT_METADATA_DISK "${CMAKE_CURRENT_SOURCE_DIR}/tests/fat16_file_commit/fixture.raw")
            set(UMICOM_FAT_METADATA_REQUIRED kernel_current_image)
        elseif(UMICOM_FAT_METADATA_ROLE STREQUAL "committed")
            set(UMICOM_FAT_METADATA_NAME kernel.riscv64.fat16_metadata_committed)
            set(UMICOM_FAT_METADATA_TARGET umicom-fat16-metadata-committed-image)
            set(UMICOM_FAT_METADATA_DISK "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-file-commit.raw")
            set(UMICOM_FAT_METADATA_REQUIRED "kernel_current_image;kernel_fat16_file_commit_fixture;kernel_fat16_file_commit_completed")
        else()
            set(UMICOM_FAT_METADATA_NAME kernel.riscv64.fat16_metadata_dirty)
            set(UMICOM_FAT_METADATA_TARGET umicom-fat16-metadata-dirty-image)
            set(UMICOM_FAT_METADATA_DISK "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-fat16-file-commit-interrupted.raw")
            set(UMICOM_FAT_METADATA_REQUIRED "kernel_current_image;kernel_fat16_file_commit_interrupted_fixture;kernel_fat16_file_commit_interrupted_completed")
        endif()
        if(UMICOM_FAT_METADATA_DISK MATCHES ",")
            message(FATAL_ERROR "The FAT16 metadata path must not contain a QEMU -drive separator comma.")
        endif()
        add_test(NAME "${UMICOM_FAT_METADATA_NAME}"
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_FAT_METADATA_QEMU=${UMICOM_QEMU_RISCV64}"
                "-DUMICOM_FAT_METADATA_IMAGE=$<TARGET_FILE:${UMICOM_FAT_METADATA_TARGET}>"
                "-DUMICOM_FAT_METADATA_DISK=${UMICOM_FAT_METADATA_DISK}"
                "-DUMICOM_FAT_METADATA_ROLE=${UMICOM_FAT_METADATA_ROLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Fat16Metadata.cmake")
        set_tests_properties("${UMICOM_FAT_METADATA_NAME}" PROPERTIES TIMEOUT 90
            FIXTURES_REQUIRED "${UMICOM_FAT_METADATA_REQUIRED}")
    endforeach()
endfunction()
cmake_language(DEFER CALL UmicomConfigureFat16MetadataImages)
