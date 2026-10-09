# Umicom Kernel writable FAT16 VFS and actual process-file-services qualification.
# One U-mode writer, a fresh read-only reader and explicit read-only admission
# refusal use a disposable image. Independent complete-media hashes qualify
# both mutations and preservation of all unrelated, free and slack bytes.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

set(UMICOM_KERNEL_DISK_WRITABLE_ORIGINAL_HASH "538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5")
set(UMICOM_KERNEL_DISK_WRITABLE_COMMITTED_HASH "2456136d78ce5956bd25672323fc792da9d670582431b52ada9a88cf6ed74847")
if(CMAKE_SCRIPT_MODE_FILE)
    file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fat16_file_commit/fixture.raw"
        UMICOM_KERNEL_DISK_WRITABLE_CHECKED_SOURCE)
    if(NOT DEFINED UMICOM_KERNEL_DISK_WRITABLE_DISK OR UMICOM_KERNEL_DISK_WRITABLE_DISK STREQUAL "")
        message(FATAL_ERROR "Writable filesystem qualification requires its disposable image path.")
    endif()
    file(REAL_PATH "${UMICOM_KERNEL_DISK_WRITABLE_DISK}" UMICOM_KERNEL_DISK_WRITABLE_REAL_DISK)
    if(UMICOM_KERNEL_DISK_WRITABLE_REAL_DISK STREQUAL UMICOM_KERNEL_DISK_WRITABLE_CHECKED_SOURCE)
        message(FATAL_ERROR "The checked source fixture cannot be a writable filesystem disposable image.")
    endif()
    if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "cleanup")
        file(REMOVE "${UMICOM_KERNEL_DISK_WRITABLE_DISK}")
        if(EXISTS "${UMICOM_KERNEL_DISK_WRITABLE_DISK}")
            message(FATAL_ERROR "The writable filesystem disposable image remains after cleanup.")
        endif()
        message("disk-writable.cleanup=absent")
        return()
    endif()
    if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "prepare")
        if(NOT DEFINED UMICOM_KERNEL_DISK_WRITABLE_SOURCE OR NOT EXISTS "${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}")
            message(FATAL_ERROR "Writable filesystem prepare requires its checked source.")
        endif()
        file(REAL_PATH "${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}" UMICOM_KERNEL_DISK_WRITABLE_REAL_SOURCE)
        if(UMICOM_KERNEL_DISK_WRITABLE_REAL_SOURCE STREQUAL UMICOM_KERNEL_DISK_WRITABLE_REAL_DISK)
            message(FATAL_ERROR "Writable filesystem prepare requires distinct real source and destination paths.")
        endif()
        file(SHA256 "${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}" UMICOM_KERNEL_DISK_WRITABLE_SOURCE_HASH)
        if(NOT UMICOM_KERNEL_DISK_WRITABLE_SOURCE_HASH STREQUAL UMICOM_KERNEL_DISK_WRITABLE_ORIGINAL_HASH)
            message(FATAL_ERROR "Writable filesystem source differs from the complete-media oracle.")
        endif()
        configure_file("${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}" "${UMICOM_KERNEL_DISK_WRITABLE_DISK}" COPYONLY)
        file(SHA256 "${UMICOM_KERNEL_DISK_WRITABLE_DISK}" UMICOM_KERNEL_DISK_WRITABLE_COPY_HASH)
        if(NOT UMICOM_KERNEL_DISK_WRITABLE_COPY_HASH STREQUAL UMICOM_KERNEL_DISK_WRITABLE_SOURCE_HASH)
            message(FATAL_ERROR "Writable filesystem disposable copy checksum mismatch.")
        endif()
        message("disk-writable.prepare=verified-original")
        return()
    endif()
    foreach(UMICOM_KERNEL_DISK_WRITABLE_PATH IN ITEMS UMICOM_KERNEL_DISK_WRITABLE_QEMU
        UMICOM_KERNEL_DISK_WRITABLE_IMAGE UMICOM_KERNEL_DISK_WRITABLE_DISK)
        if(NOT DEFINED ${UMICOM_KERNEL_DISK_WRITABLE_PATH} OR NOT EXISTS "${${UMICOM_KERNEL_DISK_WRITABLE_PATH}}")
            message(FATAL_ERROR "Writable filesystem runner requires existing ${UMICOM_KERNEL_DISK_WRITABLE_PATH}.")
        endif()
    endforeach()
    if(UMICOM_KERNEL_DISK_WRITABLE_DISK MATCHES ",")
        message(FATAL_ERROR "The QEMU drive path must not contain a comma.")
    endif()
    if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "writer")
        set(UMICOM_KERNEL_DISK_WRITABLE_BEFORE "${UMICOM_KERNEL_DISK_WRITABLE_ORIGINAL_HASH}")
        set(UMICOM_KERNEL_DISK_WRITABLE_AFTER "${UMICOM_KERNEL_DISK_WRITABLE_COMMITTED_HASH}")
        set(UMICOM_KERNEL_DISK_WRITABLE_MARKER UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READY)
        set(UMICOM_KERNEL_DISK_WRITABLE_BEGIN "disk-writable-test=begin")
        set(UMICOM_KERNEL_DISK_WRITABLE_DRIVE "file=${UMICOM_KERNEL_DISK_WRITABLE_DISK},if=none,format=raw,id=writablefs,readonly=off,cache=writeback,rerror=report,werror=report")
        set(UMICOM_KERNEL_DISK_WRITABLE_DEVICE "virtio-blk-device,drive=writablefs,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1")
    elseif(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "readback" OR
           UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "read_only")
        if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "readback")
            set(UMICOM_KERNEL_DISK_WRITABLE_BEFORE "${UMICOM_KERNEL_DISK_WRITABLE_COMMITTED_HASH}")
            set(UMICOM_KERNEL_DISK_WRITABLE_MARKER UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_READY)
            set(UMICOM_KERNEL_DISK_WRITABLE_BEGIN "disk-writable-readback-test=begin")
        else()
            set(UMICOM_KERNEL_DISK_WRITABLE_BEFORE "${UMICOM_KERNEL_DISK_WRITABLE_ORIGINAL_HASH}")
            set(UMICOM_KERNEL_DISK_WRITABLE_MARKER UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_READY)
            set(UMICOM_KERNEL_DISK_WRITABLE_BEGIN "disk-writable-read-only-test=begin")
        endif()
        set(UMICOM_KERNEL_DISK_WRITABLE_AFTER "${UMICOM_KERNEL_DISK_WRITABLE_BEFORE}")
        set(UMICOM_KERNEL_DISK_WRITABLE_DRIVE "file=${UMICOM_KERNEL_DISK_WRITABLE_DISK},if=none,format=raw,id=writablefs,readonly=on,cache=writeback,rerror=report")
        set(UMICOM_KERNEL_DISK_WRITABLE_DEVICE "virtio-blk-device,drive=writablefs,logical_block_size=512,physical_block_size=512,num-queues=1")
    else()
        message(FATAL_ERROR "Writable filesystem role must be prepare, writer, readback, read_only or cleanup.")
    endif()
    file(SHA256 "${UMICOM_KERNEL_DISK_WRITABLE_DISK}" UMICOM_KERNEL_DISK_WRITABLE_BEFORE_HASH)
    message("disk-writable.image-before=${UMICOM_KERNEL_DISK_WRITABLE_BEFORE_HASH}")
    if(NOT UMICOM_KERNEL_DISK_WRITABLE_BEFORE_HASH STREQUAL UMICOM_KERNEL_DISK_WRITABLE_BEFORE)
        message(FATAL_ERROR "Writable filesystem input image differs from its exact expected state.")
    endif()
    execute_process(COMMAND "${UMICOM_KERNEL_DISK_WRITABLE_QEMU}" -machine "virt,aclint=off"
        -bios "${UMICOM_KERNEL_DISK_WRITABLE_IMAGE}" -display none -monitor none -serial stdio
        -m 128M -smp 1 -no-reboot -global virtio-mmio.force-legacy=false
        -drive "${UMICOM_KERNEL_DISK_WRITABLE_DRIVE}" -device "${UMICOM_KERNEL_DISK_WRITABLE_DEVICE}"
        RESULT_VARIABLE UMICOM_KERNEL_DISK_WRITABLE_EXIT OUTPUT_VARIABLE UMICOM_KERNEL_DISK_WRITABLE_OUTPUT
        ERROR_VARIABLE UMICOM_KERNEL_DISK_WRITABLE_ERROR TIMEOUT 80)
    message("${UMICOM_KERNEL_DISK_WRITABLE_OUTPUT}${UMICOM_KERNEL_DISK_WRITABLE_ERROR}")
    file(SHA256 "${UMICOM_KERNEL_DISK_WRITABLE_DISK}" UMICOM_KERNEL_DISK_WRITABLE_AFTER_HASH)
    message("disk-writable.image-after=${UMICOM_KERNEL_DISK_WRITABLE_AFTER_HASH}")
    if(NOT UMICOM_KERNEL_DISK_WRITABLE_AFTER_HASH STREQUAL UMICOM_KERNEL_DISK_WRITABLE_AFTER)
        message(FATAL_ERROR "Writable filesystem output image differs from the complete independent oracle.")
    endif()
    if(NOT UMICOM_KERNEL_DISK_WRITABLE_EXIT STREQUAL "0")
        message(FATAL_ERROR "Writable filesystem guest did not exit successfully: ${UMICOM_KERNEL_DISK_WRITABLE_EXIT}")
    endif()
    string(REPLACE "\r\n" "\n" UMICOM_KERNEL_DISK_WRITABLE_TEXT
        "${UMICOM_KERNEL_DISK_WRITABLE_OUTPUT}${UMICOM_KERNEL_DISK_WRITABLE_ERROR}")
    if(UMICOM_KERNEL_DISK_WRITABLE_TEXT MATCHES "UMICOM_KERNEL_FAIL|unexpected-trap|panic|PANIC|failed|FAILED")
        message(FATAL_ERROR "Writable filesystem guest emitted a failure marker.")
    endif()
    foreach(UMICOM_KERNEL_DISK_WRITABLE_REQUIRED IN ITEMS "${UMICOM_KERNEL_DISK_WRITABLE_BEGIN}"
        "disk-writable.frame-accounting=restored" "disk-writable.machine-state=restored")
        string(FIND "\n${UMICOM_KERNEL_DISK_WRITABLE_TEXT}\n" "\n${UMICOM_KERNEL_DISK_WRITABLE_REQUIRED}\n"
            UMICOM_KERNEL_DISK_WRITABLE_POSITION)
        if(UMICOM_KERNEL_DISK_WRITABLE_POSITION LESS 0)
            message(FATAL_ERROR "Writable filesystem guest omitted ${UMICOM_KERNEL_DISK_WRITABLE_REQUIRED}.")
        endif()
    endforeach()
    if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "writer" AND
       NOT UMICOM_KERNEL_DISK_WRITABLE_TEXT MATCHES "disk-writable.accepted-commits=12")
        message(FATAL_ERROR "Writable filesystem writer did not verify twelve accepted commits.")
    endif()
    if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "readback" AND
       NOT UMICOM_KERNEL_DISK_WRITABLE_TEXT MATCHES "disk-writable.whole-image=8388608-bytes-verified")
        message(FATAL_ERROR "Writable filesystem fresh reader did not verify every media byte.")
    endif()
    string(FIND "\n${UMICOM_KERNEL_DISK_WRITABLE_TEXT}\n" "\n${UMICOM_KERNEL_DISK_WRITABLE_BEGIN}\n"
        UMICOM_KERNEL_DISK_WRITABLE_BEGIN_POSITION)
    string(FIND "\n${UMICOM_KERNEL_DISK_WRITABLE_TEXT}\n" "\n${UMICOM_KERNEL_DISK_WRITABLE_MARKER}\n"
        UMICOM_KERNEL_DISK_WRITABLE_READY_POSITION)
    string(FIND "\n${UMICOM_KERNEL_DISK_WRITABLE_TEXT}\n" "\nUMICOM_KERNEL_END\n"
        UMICOM_KERNEL_DISK_WRITABLE_END_POSITION)
    if(UMICOM_KERNEL_DISK_WRITABLE_READY_POSITION LESS UMICOM_KERNEL_DISK_WRITABLE_BEGIN_POSITION OR
       UMICOM_KERNEL_DISK_WRITABLE_END_POSITION LESS UMICOM_KERNEL_DISK_WRITABLE_READY_POSITION)
        message(FATAL_ERROR "Writable filesystem guest requires exact begin, readiness and end lines in order.")
    endif()
    message("disk-writable.backing-image=exact-independent-result")
    return()
endif()

target_sources(umicom-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/fat16_writable_provider.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_writable_filesystem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_writable_filesystem_console.c")
# Source properties reach every clone of console_shell.c, including the nested
# trap image. A target-local define would leave that shared source graph split.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/kernel/console_shell.c" APPEND
    PROPERTY COMPILE_DEFINITIONS UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM=1)
if(NOT BUILD_TESTING)
    return()
endif()
add_executable(umicom-disk-file-program
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/disk_file_client/entry.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/programs/disk_file_client/main.c")
target_include_directories(umicom-disk-file-program PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
get_target_property(UMICOM_KERNEL_DISK_FILE_OPTIONS umicom-kernel COMPILE_OPTIONS)
target_compile_options(umicom-disk-file-program PRIVATE ${UMICOM_KERNEL_DISK_FILE_OPTIONS}
    -fno-jump-tables -fno-vectorize -fno-slp-vectorize)
set(UMICOM_KERNEL_DISK_FILE_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/programs/disk_file_client/linker.ld")
target_link_options(umicom-disk-file-program PRIVATE
    -march=rv64imac_zicsr -mabi=lp64 -mcmodel=medany -nostdlib -fuse-ld=lld
    "-Wl,-T,${UMICOM_KERNEL_DISK_FILE_LINKER}" -Wl,--build-id=none -Wl,--no-relax)
set_target_properties(umicom-disk-file-program PROPERTIES OUTPUT_NAME "umicom-disk-file-client"
    SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/programs"
    LINK_DEPENDS "${UMICOM_KERNEL_DISK_FILE_LINKER}")
set(UMICOM_KERNEL_DISK_FILE_EXECUTABLE_FILE "${CMAKE_CURRENT_BINARY_DIR}/programs/umicom-disk-file-client.elf")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedDiskFileProgram.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_disk_file_program.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_disk_file_program.S"
    PROPERTIES OBJECT_DEPENDS "${UMICOM_KERNEL_DISK_FILE_EXECUTABLE_FILE}")

function(UmicomKernelConfigureDiskWritableFilesystemImages)
    # The final source graph already contains the common firmware bridge. Its
    # dedicated user program is only an ELF carrier in these diagnostic images.
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_SOURCES umicom-kernel SOURCES)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_INCLUDES umicom-kernel INCLUDE_DIRECTORIES)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_OPTIONS umicom-kernel COMPILE_OPTIONS)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_LINK_OPTIONS umicom-kernel LINK_OPTIONS)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_DEFINITIONS umicom-kernel COMPILE_DEFINITIONS)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_LIBRARIES umicom-kernel LINK_LIBRARIES)
    get_target_property(UMICOM_KERNEL_DISK_WRITABLE_DEPENDENCIES umicom-kernel MANUALLY_ADDED_DEPENDENCIES)
    foreach(UMICOM_KERNEL_DISK_WRITABLE_ROLE IN ITEMS writer readback read_only)
        if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "writer")
            set(UMICOM_KERNEL_DISK_WRITABLE_OUTPUT umicom-disk-writable-filesystem)
            set(UMICOM_KERNEL_DISK_WRITABLE_DEFINITION UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST=1)
        elseif(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "readback")
            set(UMICOM_KERNEL_DISK_WRITABLE_OUTPUT umicom-disk-writable-filesystem-readback)
            set(UMICOM_KERNEL_DISK_WRITABLE_DEFINITION UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST=1)
        else()
            set(UMICOM_KERNEL_DISK_WRITABLE_OUTPUT umicom-disk-writable-filesystem-read-only)
            set(UMICOM_KERNEL_DISK_WRITABLE_DEFINITION UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_TEST=1)
        endif()
        set(UMICOM_KERNEL_DISK_WRITABLE_TARGET "${UMICOM_KERNEL_DISK_WRITABLE_OUTPUT}-image")
        add_executable(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} ${UMICOM_KERNEL_DISK_WRITABLE_SOURCES}
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_writable_filesystem_boot.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/kernel/disk_writable_filesystem_validation.c"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_disk_file_program.S")
        target_include_directories(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_INCLUDES})
        target_compile_options(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_OPTIONS})
        target_compile_definitions(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_DEFINITION})
        if(UMICOM_KERNEL_DISK_WRITABLE_DEFINITIONS)
            target_compile_definitions(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_DEFINITIONS})
        endif()
        if(UMICOM_KERNEL_DISK_WRITABLE_LIBRARIES)
            target_link_libraries(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_LIBRARIES})
        endif()
        target_link_options(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PRIVATE ${UMICOM_KERNEL_DISK_WRITABLE_LINK_OPTIONS}
            "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${UMICOM_KERNEL_DISK_WRITABLE_OUTPUT}.map")
        set_target_properties(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} PROPERTIES
            OUTPUT_NAME "${UMICOM_KERNEL_DISK_WRITABLE_OUTPUT}" SUFFIX ".elf"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
            LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}")
        add_dependencies(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} umicom-disk-file-program)
        if(UMICOM_KERNEL_DISK_WRITABLE_DEPENDENCIES)
            add_dependencies(${UMICOM_KERNEL_DISK_WRITABLE_TARGET} ${UMICOM_KERNEL_DISK_WRITABLE_DEPENDENCIES})
        endif()
    endforeach()
    # Publish reverse dependencies only after every snapshot is complete.
    add_dependencies(umicom-kernel umicom-disk-writable-filesystem-image
        umicom-disk-writable-filesystem-readback-image umicom-disk-writable-filesystem-read-only-image)
    if(NOT UMICOM_QEMU_RISCV64)
        return()
    endif()
    set(UMICOM_KERNEL_DISK_WRITABLE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fat16_file_commit/fixture.raw")
    file(SHA256 "${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}" UMICOM_KERNEL_DISK_WRITABLE_SOURCE_HASH)
    if(NOT UMICOM_KERNEL_DISK_WRITABLE_SOURCE_HASH STREQUAL UMICOM_KERNEL_DISK_WRITABLE_ORIGINAL_HASH)
        message(FATAL_ERROR "The writable filesystem synthetic source does not match its recorded SHA-256.")
    endif()
    set(UMICOM_KERNEL_DISK_WRITABLE_DISK "${CMAKE_CURRENT_BINARY_DIR}/fixtures/umicom-disk-writable-filesystem.raw")
    set(UMICOM_KERNEL_DISK_WRITABLE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DiskWritableFilesystem.cmake")
    set(UMICOM_KERNEL_DISK_WRITABLE_BASE kernel.riscv64.disk_writable_filesystem)
    add_test(NAME ${UMICOM_KERNEL_DISK_WRITABLE_BASE}_prepare COMMAND "${CMAKE_COMMAND}"
        "-DUMICOM_KERNEL_DISK_WRITABLE_SOURCE=${UMICOM_KERNEL_DISK_WRITABLE_SOURCE}"
        "-DUMICOM_KERNEL_DISK_WRITABLE_DISK=${UMICOM_KERNEL_DISK_WRITABLE_DISK}"
        -DUMICOM_KERNEL_DISK_WRITABLE_ROLE=prepare -P "${UMICOM_KERNEL_DISK_WRITABLE_SCRIPT}")
    set_tests_properties(${UMICOM_KERNEL_DISK_WRITABLE_BASE}_prepare PROPERTIES TIMEOUT 10
        FIXTURES_REQUIRED kernel_current_image FIXTURES_SETUP kernel_disk_writable_fixture)
    foreach(UMICOM_KERNEL_DISK_WRITABLE_ROLE IN ITEMS read_only writer readback)
        if(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "writer")
            set(UMICOM_KERNEL_DISK_WRITABLE_TARGET umicom-disk-writable-filesystem-image)
            set(UMICOM_KERNEL_DISK_WRITABLE_REQUIRED "kernel_current_image;kernel_disk_writable_fixture;kernel_disk_writable_read_only_checked")
        elseif(UMICOM_KERNEL_DISK_WRITABLE_ROLE STREQUAL "readback")
            set(UMICOM_KERNEL_DISK_WRITABLE_TARGET umicom-disk-writable-filesystem-readback-image)
            set(UMICOM_KERNEL_DISK_WRITABLE_REQUIRED "kernel_current_image;kernel_disk_writable_fixture;kernel_disk_writable_committed")
        else()
            set(UMICOM_KERNEL_DISK_WRITABLE_TARGET umicom-disk-writable-filesystem-read-only-image)
            set(UMICOM_KERNEL_DISK_WRITABLE_REQUIRED "kernel_current_image;kernel_disk_writable_fixture")
        endif()
        add_test(NAME ${UMICOM_KERNEL_DISK_WRITABLE_BASE}_${UMICOM_KERNEL_DISK_WRITABLE_ROLE}
            COMMAND "${CMAKE_COMMAND}" "-DUMICOM_KERNEL_DISK_WRITABLE_QEMU=${UMICOM_QEMU_RISCV64}"
            "-DUMICOM_KERNEL_DISK_WRITABLE_IMAGE=$<TARGET_FILE:${UMICOM_KERNEL_DISK_WRITABLE_TARGET}>"
            "-DUMICOM_KERNEL_DISK_WRITABLE_DISK=${UMICOM_KERNEL_DISK_WRITABLE_DISK}"
            "-DUMICOM_KERNEL_DISK_WRITABLE_ROLE=${UMICOM_KERNEL_DISK_WRITABLE_ROLE}"
            -P "${UMICOM_KERNEL_DISK_WRITABLE_SCRIPT}")
        set_tests_properties(${UMICOM_KERNEL_DISK_WRITABLE_BASE}_${UMICOM_KERNEL_DISK_WRITABLE_ROLE}
            PROPERTIES TIMEOUT 90 FIXTURES_REQUIRED "${UMICOM_KERNEL_DISK_WRITABLE_REQUIRED}")
    endforeach()
    set_tests_properties(${UMICOM_KERNEL_DISK_WRITABLE_BASE}_read_only PROPERTIES
        FIXTURES_SETUP kernel_disk_writable_read_only_checked)
    set_tests_properties(${UMICOM_KERNEL_DISK_WRITABLE_BASE}_writer PROPERTIES
        FIXTURES_SETUP kernel_disk_writable_committed)
    add_test(NAME ${UMICOM_KERNEL_DISK_WRITABLE_BASE}_cleanup COMMAND "${CMAKE_COMMAND}"
        "-DUMICOM_KERNEL_DISK_WRITABLE_DISK=${UMICOM_KERNEL_DISK_WRITABLE_DISK}"
        -DUMICOM_KERNEL_DISK_WRITABLE_ROLE=cleanup -P "${UMICOM_KERNEL_DISK_WRITABLE_SCRIPT}")
    set_tests_properties(${UMICOM_KERNEL_DISK_WRITABLE_BASE}_cleanup PROPERTIES TIMEOUT 10
        FIXTURES_CLEANUP kernel_disk_writable_fixture)
endfunction()
cmake_language(DEFER CALL UmicomKernelConfigureDiskWritableFilesystemImages)
