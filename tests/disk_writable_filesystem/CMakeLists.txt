# Umicom Kernel writable VFS, descriptor and retained disk ownership tests.
# The existing independent VirtIO model supplies cached/eager persistence.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomDiskWritableFilesystemTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_KERNEL_DISK_WRITABLE_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-disk-writable-filesystem-tests writable_tests.c
    "${UMICOM_ROOT}/kernel/fat16_writable_provider.c"
    "${UMICOM_ROOT}/kernel/disk_writable_filesystem.c"
    "${UMICOM_ROOT}/kernel/disk_writable_filesystem_console.c"
    "${UMICOM_ROOT}/kernel/fat16_update.c"
    "${UMICOM_ROOT}/kernel/fat16_update_console.c"
    "${UMICOM_ROOT}/kernel/console_line.c"
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_partitions.c"
    "${UMICOM_ROOT}/kernel/fat16_provider.c"
    "${UMICOM_ROOT}/kernel/disk_filesystem.c"
    "${UMICOM_ROOT}/kernel/vfs.c"
    "${UMICOM_ROOT}/kernel/virtio_block.c"
    "${UMICOM_ROOT}/kernel/block_platform_policy.c"
    "${UMICOM_ROOT}/kernel/block_console.c"
    "${UMICOM_ROOT}/kernel/virtio_block_validation.c"
    "${UMICOM_ROOT}/kernel/physical_memory.c"
    "${UMICOM_ROOT}/kernel/address.c")
target_include_directories(umicom-disk-writable-filesystem-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-disk-writable-filesystem-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-disk-writable-filesystem-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_KERNEL_DISK_WRITABLE_SANITIZERS)
    target_compile_options(umicom-disk-writable-filesystem-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-disk-writable-filesystem-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_KERNEL_DISK_WRITABLE_CASES
    mount.lifetime mount.readonly mount.no-flush mount.bad-calendar mount.exclusive
    mount.reset-retry mount.free-retry mount.failed-admission
    write.overwrite write.extend write.maximum write.append-interleaved write.hole
    write.zero write.too-large write.readonly
    resize.zero-growth resize.shrink resize.bound
    rights.read-only rights.duplicate
    identity.case identity.reuse identity.pinned-cache identity.all-pinned identity.exhausted
    directory.changed directory.write-stable directory.refusal-stable directory.epoch-exhausted
    directory.remove-open directory.nested
    failure.read failure.timeout failure.write failure.flush failure.drop failure.cut-cached failure.cut-eager
    guards.provider-output guards.domain-output guards.dma-output guards.overlap guards.alignment
    guards.busy guards.drift guards.updater-state guards.slot-state guards.time
    console.sequence console.arguments console.failure-close console.reentry console.binary
    console.partition-typo console.arity console.numeric console.calendar console.unmounted
    console.argument-reentry console.mounted-arguments console.owner-diagnostics
    console.lifetime-hints console.failed-admission-hint)
foreach(UMICOM_KERNEL_DISK_WRITABLE_CASE IN LISTS UMICOM_KERNEL_DISK_WRITABLE_CASES)
    add_test(NAME "disk-writable-filesystem.${UMICOM_KERNEL_DISK_WRITABLE_CASE}"
        COMMAND umicom-disk-writable-filesystem-tests "${UMICOM_KERNEL_DISK_WRITABLE_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../fat16_file_commit/fixture.raw")
    set_tests_properties("disk-writable-filesystem.${UMICOM_KERNEL_DISK_WRITABLE_CASE}" PROPERTIES TIMEOUT 180)
endforeach()

# Independently encoded complete-image expectations are checked before their
# constants are trusted by the guest CTest persistence sequence.
add_executable(umicom-disk-writable-filesystem-image-verify image_verify.c)
target_include_directories(umicom-disk-writable-filesystem-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-disk-writable-filesystem-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
