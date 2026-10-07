# Umicom Kernel native read-only filesystem provider and mount qualification.
# Production VFS, FAT16 interpretation and VirtIO ownership are linked directly.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomDiskFilesystemTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_DISK_FILESYSTEM_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)

set(UMICOM_DISK_FILESYSTEM_PROVIDER_SOURCES
    "${UMICOM_ROOT}/kernel/vfs.c"
    "${UMICOM_ROOT}/kernel/fat16_provider.c"
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_partitions.c")
add_executable(umicom-disk-filesystem-provider-tests provider_tests.c
    ${UMICOM_DISK_FILESYSTEM_PROVIDER_SOURCES})
add_executable(umicom-disk-filesystem-transport-tests transport_tests.c
    ${UMICOM_DISK_FILESYSTEM_PROVIDER_SOURCES}
    "${UMICOM_ROOT}/kernel/disk_filesystem.c"
    "${UMICOM_ROOT}/kernel/disk_filesystem_console.c"
    "${UMICOM_ROOT}/kernel/disk_filesystem_validation.c"
    "${UMICOM_ROOT}/kernel/virtio_block.c"
    "${UMICOM_ROOT}/kernel/block_platform_policy.c"
    "${UMICOM_ROOT}/kernel/block_console.c"
    "${UMICOM_ROOT}/kernel/virtio_block_validation.c"
    "${UMICOM_ROOT}/kernel/physical_memory.c"
    "${UMICOM_ROOT}/kernel/address.c"
    "${UMICOM_ROOT}/kernel/console_line.c")
# The existing MMIO model checks reset acknowledgement and page scrubbing at
# the real allocator boundary. This suite neither copies nor replaces it.
target_link_options(umicom-disk-filesystem-transport-tests PRIVATE
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
foreach(UMICOM_DISK_FILESYSTEM_TARGET IN ITEMS
    umicom-disk-filesystem-provider-tests umicom-disk-filesystem-transport-tests)
    target_include_directories(${UMICOM_DISK_FILESYSTEM_TARGET} PRIVATE "${UMICOM_ROOT}/include")
    target_compile_options(${UMICOM_DISK_FILESYSTEM_TARGET} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
    if(UMICOM_DISK_FILESYSTEM_SANITIZERS)
        target_compile_options(${UMICOM_DISK_FILESYSTEM_TARGET} PRIVATE -fsanitize=address,undefined)
        target_link_options(${UMICOM_DISK_FILESYSTEM_TARGET} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()

# Each process receives a private in-memory copy of the existing disposable
# fixture. Tests never rewrite the packaged disk or use any developer data.
set(UMICOM_DISK_FILESYSTEM_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
set(UMICOM_DISK_FILESYSTEM_PROVIDER_CASES
    open_lifetime null_policy admission_retry invalid_arguments
    case_identity distinct_empty_nodes nested_lookup fragmented_read
    seek_duplicate separate_positions eof_and_zero oversized_read
    failed_reads_atomic directory_iteration directory_epoch directory_failure_cursor
    cache_eviction_stale_ids pinned_cache_survives all_nodes_pinned_capacity
    node_identity_exhaustion provider_busy_close vfs_busy_unmount
    empty_client_unmount copied_owner malformed_media unsupported_media
    path_refusals policy_reentry policy_refusal reader_reentry
    mutation_callbacks broad_client_mutations status_names)
foreach(UMICOM_DISK_FILESYSTEM_CASE IN LISTS UMICOM_DISK_FILESYSTEM_PROVIDER_CASES)
    add_test(NAME disk-filesystem.provider.${UMICOM_DISK_FILESYSTEM_CASE}
        COMMAND umicom-disk-filesystem-provider-tests
        "${UMICOM_DISK_FILESYSTEM_CASE}" "${UMICOM_DISK_FILESYSTEM_FIXTURE}")
    set_tests_properties(disk-filesystem.provider.${UMICOM_DISK_FILESYSTEM_CASE} PROPERTIES TIMEOUT 60)
endforeach()
set(UMICOM_DISK_FILESYSTEM_TRANSPORT_CASES
    guest_sequence mounted_read idle_mount_budget shared_position
    empty_client_busy open_file_busy duplicate_busy single_lifetime
    unsafe_admission readonly_clients exclusive_slot absent_device
    bad_media_cleanup failed_open_retained reset_retry partial_release_retry
    read_error_cleanup read_reset_pending stopped_clock backward_clock
    operation_timeout admission_retry allocation_failure fresh_mount_lifetimes
    final_sector_deadline final_sector_clock_rollback extra_provider_pin second_vfs_pin
    console_persistent_commands console_double_mount console_absent_retry
    console_bad_media_retry console_platform_retry console_unmount_retry
    console_poweroff_retry console_binary_escape console_invalid_arguments)
foreach(UMICOM_DISK_FILESYSTEM_CASE IN LISTS UMICOM_DISK_FILESYSTEM_TRANSPORT_CASES)
    add_test(NAME disk-filesystem.transport.${UMICOM_DISK_FILESYSTEM_CASE}
        COMMAND umicom-disk-filesystem-transport-tests
        "${UMICOM_DISK_FILESYSTEM_CASE}" "${UMICOM_DISK_FILESYSTEM_FIXTURE}")
    set_tests_properties(disk-filesystem.transport.${UMICOM_DISK_FILESYSTEM_CASE} PROPERTIES TIMEOUT 60)
endforeach()
