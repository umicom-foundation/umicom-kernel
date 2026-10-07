# Umicom Kernel native read-only partition and filesystem qualification.
# No production parser or block driver is substituted in the transport suite.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomDiskInspectionTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_DISK_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-disk-tests disk_inspection_tests.c
    "${UMICOM_ROOT}/kernel/disk_partitions.c"
    "${UMICOM_ROOT}/kernel/fat16_inspector.c")
add_executable(umicom-disk-transport-tests transport_tests.c
    "${UMICOM_ROOT}/kernel/disk_partitions.c"
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_console.c"
    "${UMICOM_ROOT}/kernel/disk_inspection_validation.c"
    "${UMICOM_ROOT}/kernel/virtio_block.c"
    "${UMICOM_ROOT}/kernel/block_platform_policy.c"
    "${UMICOM_ROOT}/kernel/block_console.c"
    "${UMICOM_ROOT}/kernel/virtio_block_validation.c"
    "${UMICOM_ROOT}/kernel/physical_memory.c"
    "${UMICOM_ROOT}/kernel/address.c"
    "${UMICOM_ROOT}/kernel/console_line.c")
# Reuse the previous model's narrowly wrapped allocator failure injection.
target_link_options(umicom-disk-transport-tests PRIVATE
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
foreach(UMICOM_DISK_TARGET IN ITEMS umicom-disk-tests umicom-disk-transport-tests)
    target_include_directories(${UMICOM_DISK_TARGET} PRIVATE "${UMICOM_ROOT}/include")
    target_compile_options(${UMICOM_DISK_TARGET} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
    if(UMICOM_DISK_SANITIZERS)
        target_compile_options(${UMICOM_DISK_TARGET} PRIVATE -fsanitize=address,undefined)
        target_link_options(${UMICOM_DISK_TARGET} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()
add_executable(umicom-disk-fixture EXCLUDE_FROM_ALL fixture_builder.c)
target_compile_options(umicom-disk-fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)
set(UMICOM_DISK_CASES
    mbr_valid
    sparse_partition_slot
    two_sector_clusters
    mbr_signature
    mbr_io
    mbr_gpt
    mbr_extended
    mbr_empty
    mbr_partial_empty
    mbr_flag
    mbr_zero_start
    mbr_outside
    mbr_overlap
    mbr_adjacent
    mbr_wide_arithmetic
    valid_geometry
    invalid_arguments
    partition_selection
    wrong_partition_type
    bpb_signature
    sector_size
    cluster_geometry
    single_fat
    root_geometry
    missing_reserved
    missing_fat_size
    ambiguous_size
    zero_size
    hidden_offset
    volume_outside_partition
    no_data
    fat12_refused
    fat32_refused
    fat_too_short
    label_not_type
    label_sanitised
    media_and_reserved
    dirty_flags
    fat_mirror_open
    open_io_unchanged
    owner_lifetime
    root_and_nested
    path_grammar
    missing_and_wrong_type
    long_name_skipped
    deleted_ignored
    short_name_invalid
    short_name_padding
    duplicate_short_alias
    entry_attributes
    entry_high_cluster
    entry_cluster_bounds
    zero_file_cluster
    directory_size
    directory_dot_corrupt
    listing_capacity
    directory_scan_limit
    directory_cycle
    fragmented_read
    eof_and_empty
    bad_chain_codes
    file_cycle
    short_chain
    long_chain
    chain_limit
    later_mirror_mismatch
    all_read_failures_unchanged
    list_io_unchanged
    stat_failure_unchanged
    reentry_refused
    repeated_lifetimes
    deterministic_mutations
)
foreach(UMICOM_DISK_CASE IN LISTS UMICOM_DISK_CASES)
    add_test(NAME disk.${UMICOM_DISK_CASE}
        COMMAND umicom-disk-tests "${UMICOM_DISK_CASE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixture.raw")
    set_tests_properties(disk.${UMICOM_DISK_CASE} PROPERTIES TIMEOUT 60)
endforeach()
set(UMICOM_DISK_TRANSPORT_CASES
    guest_sequence
    driver_fragmented
    console_partitions
    console_info
    console_list
    console_cat
    console_binary_escape
    console_bad_media
    console_bad_completion
    console_close_retry
    driver_io_unchanged
    console_commands
)
foreach(UMICOM_DISK_CASE IN LISTS UMICOM_DISK_TRANSPORT_CASES)
    add_test(NAME disk.transport.${UMICOM_DISK_CASE}
        COMMAND umicom-disk-transport-tests "${UMICOM_DISK_CASE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixture.raw")
    set_tests_properties(disk.transport.${UMICOM_DISK_CASE} PROPERTIES TIMEOUT 60)
endforeach()
