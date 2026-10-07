# Umicom Kernel native persisted-metadata qualification.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16MetadataTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_METADATA_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-metadata-tests metadata_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-metadata-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-metadata-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_METADATA_SANITIZERS)
    target_compile_options(umicom-fat16-metadata-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-metadata-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_METADATA_CASES oversized_fat)
foreach(UMICOM_CASE IN ITEMS all_dates all_times null alignment extent_overflow)
    list(APPEND UMICOM_METADATA_CASES "decode.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS ordinary archive_clear hidden_system casefold root_later_sector root_last_entry nested
    fragmented_parent nested_last_entry long_names_skipped deleted_records orphan_allocation target_cycle
    target_short_chain creation_access_ignored path_snapshot callback_reentry owner_reuse)
    list(APPEND UMICOM_METADATA_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS root directory empty_file nested_readonly)
    list(APPEND UMICOM_METADATA_CASES "object.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS absent midnight date_missing month_zero month_high day_zero april_31 century_nonleap
    hour_24 minute_60 second_60 second_62)
    list(APPEND UMICOM_METADATA_CASES "raw.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS null_owner owner_alignment owner_overflow null_result result_alignment result_overflow
    result_owner result_owner_scratch path_owner path_owner_scratch path_result null_path path_overflow unterminated_path
    copied_owner busy_owner closed_owner)
    list(APPEND UMICOM_METADATA_CASES "guard.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS terminates_before_output terminator_inside_output scans_into_output)
    list(APPEND UMICOM_METADATA_CASES "boundary.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS empty relative double_slash trailing_slash parent dot long_alias long_extension
    double_extension backslash non_ascii)
    list(APPEND UMICOM_METADATA_CASES "grammar.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS missing file_as_directory unknown_attribute reserved_case_bits high_cluster cluster_outside
    nonempty_zero_cluster empty_nonzero_cluster directory_size duplicate_alias invalid_name_after_target parent_cycle
    parent_free_link parent_mirror_mismatch dot_corrupt directory_capacity directory_scan_limit)
    list(APPEND UMICOM_METADATA_CASES "refusal.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS reader_null capacity_zero partition_zero partition_outside volume_outside cluster_zero
    cluster_nonpower cluster_large fat_zero fat_start root_alignment root_limit root_start data_start cluster_count)
    list(APPEND UMICOM_METADATA_CASES "geometry.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS root_all_reads nested_all_reads)
    list(APPEND UMICOM_METADATA_CASES "failure.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS attribute case_bits high_cluster cluster size name)
    list(APPEND UMICOM_METADATA_CASES "recheck.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN LISTS UMICOM_METADATA_CASES)
    add_test(NAME "fat16-metadata.${UMICOM_CASE}" COMMAND umicom-fat16-metadata-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-metadata.${UMICOM_CASE}" PROPERTIES TIMEOUT 60)
endforeach()

add_executable(umicom-fat16-metadata-transport-tests transport_tests.c
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
target_include_directories(umicom-fat16-metadata-transport-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-metadata-transport-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-fat16-metadata-transport-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_FAT16_METADATA_SANITIZERS)
    target_compile_options(umicom-fat16-metadata-transport-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-metadata-transport-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_METADATA_TRANSPORT_CASES all_read_failures)
foreach(UMICOM_CASE IN ITEMS ordinary absent invalid midnight attributes archive_clear root readonly_file path_snapshot
    queue_reentry policy_reentry timer_reentry output_reentry command_dispatch operation_dispatch)
    list(APPEND UMICOM_METADATA_TRANSPORT_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS writable_refused bad_completion unsafe_context allocation_first allocation_second dirty
    bad_signature missing malicious_name)
    list(APPEND UMICOM_METADATA_TRANSPORT_CASES "failure.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS reset_retry retained_blocks_next first_release_retry second_release_retry)
    list(APPEND UMICOM_METADATA_TRANSPORT_CASES "cleanup.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS last_read_deadline last_read_rollback acceptance_deadline acceptance_rollback
    after_close_deadline after_close_rollback)
    list(APPEND UMICOM_METADATA_TRANSPORT_CASES "clock.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS commands null_path path_overflow null_output slot partition empty_path relative_path unterminated_path)
    list(APPEND UMICOM_METADATA_TRANSPORT_CASES "arguments.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN LISTS UMICOM_METADATA_TRANSPORT_CASES)
    add_test(NAME "fat16-metadata.transport.${UMICOM_CASE}" COMMAND umicom-fat16-metadata-transport-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-metadata.transport.${UMICOM_CASE}" PROPERTIES TIMEOUT 60)
endforeach()
