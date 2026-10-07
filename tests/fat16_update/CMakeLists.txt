# Umicom Kernel native qualification of explicit existing-file data updates.
# The production parser, updater, transport and allocator are linked directly.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16UpdateTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_UPDATE_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-update-tests update_tests.c
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
target_include_directories(umicom-fat16-update-tests PRIVATE "${UMICOM_ROOT}/include")
target_link_options(umicom-fat16-update-tests PRIVATE
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
add_executable(umicom-fat16-update-image-verify image_verify.c)
foreach(UMICOM_FAT16_UPDATE_TARGET IN ITEMS umicom-fat16-update-tests umicom-fat16-update-image-verify)
    target_include_directories(${UMICOM_FAT16_UPDATE_TARGET} PRIVATE "${UMICOM_ROOT}/include")
    target_compile_options(${UMICOM_FAT16_UPDATE_TARGET} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
    if(UMICOM_FAT16_UPDATE_SANITIZERS)
        target_compile_options(${UMICOM_FAT16_UPDATE_TARGET} PRIVATE -fsanitize=address,undefined)
        target_link_options(${UMICOM_FAT16_UPDATE_TARGET} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()
set(UMICOM_FAT16_UPDATE_CASES
    status_names callback_reentry readonly_vfs_boundary known_prefix_no_later_submission
    flush_output_ownership revalidate_between_writes repeated_successful_writes policy_reentry
    stopped_preflight_clock backward_preflight_clock)
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    single_byte unaligned_fragmented whole_file last_byte aligned_sector nine_sectors
    hidden_system_target deleted_lfn deleted_stale_cluster end_marker_stale_entry
    matching_fat_padding casefold_path idle_deadline_restart)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "success.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    file_file_crosslink file_directory_crosslink shared_tail orphan_to_target orphan_allocation
    allocated_empty_file directory_cycle directory_wrong_parent directory_missing_dot directory_missing_parent
    directory_duplicate_dot directory_dot_file directory_dot_high_cluster root_dot root_label_cluster
    root_label_size root_label_directory nested_volume_label live_lfn bad_cluster free_chain_link
    reserved_chain_link target_chain_cycle short_chain surplus_chain dirty_volume hard_error_volume
    fat_padding_mismatch remote_fat_mismatch unknown_attribute archive_clear target_readonly duplicate_name
    directory_limit object_limit depth_limit chain_limit io_budget)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "format.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS first_write_failure middle_write_failure last_write_failure uncertain_flush_and_retry)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "partial.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS final_completion_deadline final_completion_rollback outer_deadline outer_rollback)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "clock.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS first_preflight_read_failure last_preflight_read_failure
    final_preflight_clock_deadline final_preflight_clock_rollback)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "read.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    zero_close readonly_admission_retry no_flush_admission_retry failed_open_retained allocation_failure_retry
    unsafe_admission_retry single_lifetime reset_retry release_retry second_release_retry timeout_retained close_without_flush
    power_loss_unflushed power_loss_flushed empty_flush)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "lifetime.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    input_owner_alias input_domain_alias input_dma_alias other_slot_dma_alias input_span_overflow result_owner_alias
    result_alignment result_span_overflow result_domain_alias result_dma_alias input_result_alias
    path_input_alias path_input_nonterminated path_result_nonterminated path_owner_alias path_domain_alias path_dma_alias
    owner_alignment unterminated_path copied_owner
    busy_owner busy_domain unsafe_write null_and_oversized)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "ownership.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS domain_alias dma_alias alignment numeric_limits)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "open_arguments.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    eof past_eof extends_file offset_overflow empty_file directory_target missing_file readonly_readme
    readonly_nested relative_path dotdot_path trailing_slash root_target)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "range.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS
    complete_plan input_alias plan_alias workspace_alias path_alias alignment span_overflow dirty_workspace
    copied_workspace geometry_divisor fat_sector_limit reentry input_snapshot read_failure_atomic io_budget)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "plan.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS final_deadline final_rollback outer_deadline outer_rollback)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "flush_clock.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN ITEMS before_open invalid_arguments no_device_retry readonly_retry platform_retry
    failed_open_retained wrong_shell empty_text reset_retry release_retry historical_uncertainty command_sequence
    close_without_flush callback_reentry output_reentry)
    list(APPEND UMICOM_FAT16_UPDATE_CASES "console.${UMICOM_FAT16_UPDATE_CASE}")
endforeach()
foreach(UMICOM_FAT16_UPDATE_CASE IN LISTS UMICOM_FAT16_UPDATE_CASES)
    add_test(NAME "fat16-update.${UMICOM_FAT16_UPDATE_CASE}"
        COMMAND umicom-fat16-update-tests "${UMICOM_FAT16_UPDATE_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-update.${UMICOM_FAT16_UPDATE_CASE}" PROPERTIES TIMEOUT 60)
endforeach()
