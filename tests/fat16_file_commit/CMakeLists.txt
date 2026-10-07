# Umicom Kernel native qualification of timestamped, ordered FAT16 file commits.
# Real parser, updater, block transport and allocator; independent media model.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16FileCommitTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_FILE_COMMIT_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-file-commit-tests file_commit_tests.c
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
target_include_directories(umicom-fat16-file-commit-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-file-commit-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-fat16-file-commit-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_FAT16_FILE_COMMIT_SANITIZERS)
    target_compile_options(umicom-fat16-file-commit-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-file-commit-tests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(umicom-fat16-file-commit-image-verify image_verify.c)
target_include_directories(umicom-fat16-file-commit-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-file-commit-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_FILE_COMMIT_SANITIZERS)
    target_compile_options(umicom-fat16-file-commit-image-verify PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-file-commit-image-verify PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_FAT16_FILE_COMMIT_CASES callback_reentry policy_reentry stopped_clock backward_clock
    revalidate_before_stage)
foreach(UMICOM_CASE IN ITEMS archive_clear archive_set hidden_system single_byte fragmented whole_file last_byte
    aligned_sector nine_sectors write_through root_later_sector root_last_entry nested_first
    nested_fragmented nested_last_entry preserved_fields matching_metadata casefold idle_intervals)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS minimum maximum leap_1980 leap_2000 leap_2004 leap_2104 common_february
    even_second odd_second midnight end_of_day)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "calendar.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS year_low year_high month_zero month_high day_zero day_high april_31 june_31
    september_31 november_31 common_february_29 century_2100_february_29 leap_february_30 hour_high minute_high second_high)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "invalid_calendar.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS file_crosslink directory_crosslink shared_tail orphan_to_target orphan_allocation
    allocated_empty directory_cycle wrong_parent missing_dot missing_parent root_label_cluster nested_label
    live_lfn bad_cluster free_link reserved_link target_cycle short_chain surplus_chain dirty hard_error
    padding_mismatch remote_fat_mismatch unknown_attribute readonly duplicate_name
    directory_limit object_limit depth_limit chain_limit io_budget)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "format.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS dirty_mirror dirty_primary data_first data_middle data_last directory clean_mirror clean_primary)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "write_error.${UMICOM_CASE}" "dropped_write.${UMICOM_CASE}" "altered_write.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS dirty_mirror dirty_primary data directory clean_mirror clean_primary)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "flush_error.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS dirty_primary dirty_mirror data_first data_middle data_last directory
    finish_primary finish_mirror finish_first finish_middle finish_last finish_directory clean_primary clean_mirror)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "read_error.${UMICOM_CASE}" "read_mismatch.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE RANGE 1 14)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "cut.cached.${UMICOM_CASE}" "cut.eager.${UMICOM_CASE}")
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "clock.outer_deadline.${UMICOM_CASE}" "clock.outer_rollback.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS 1 4 7 9 10 14)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "clock.inner_deadline.${UMICOM_CASE}" "clock.inner_rollback.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS result_owner result_outer_plan result_domain result_dma result_queue result_alignment
    result_overflow result_null other_slot_dma owner_alignment owner_null copied_owner busy_owner busy_embedded
    busy_updater busy_file_workspace busy_base_workspace busy_volume busy_domain unsafe_context)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "ownership.${UMICOM_CASE}" "finish_ownership.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS input_owner input_outer_plan input_domain input_dma input_queue input_result input_time
    input_overflow input_null input_zero input_oversized path_input path_input_nonterminated path_result_nonterminated
    path_time path_outer_plan path_domain path_dma path_unterminated path_null time_owner time_outer_plan time_domain
    time_dma time_queue time_result time_alignment time_overflow time_null)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "ownership.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS domain_alias dma_alias alignment null slot partition timeout_zero timeout_excess nonzero_storage)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "open_arguments.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS eof past_eof extends_file offset_overflow empty_file directory missing readonly
    nested_readonly relative parent_component trailing_slash root)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "range.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS zero_close readonly_retry no_flush_retry first_allocation_retry second_allocation_retry
    unsafe_open_retry failed_open_retained inspection_retry ready_close finish_before_stage timeout_retained
    stage_twice reset_retry first_release_retry second_release_retry unsafe_close_retry single_lifetime finish_twice staged_close)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "lifetime.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS first_read last_read directory_mismatch last_clock_deadline last_clock_rollback)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "preflight.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS stage_deadline stage_rollback finish_deadline finish_rollback)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "acceptance_clock.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS primary_header mirror_header first_neighbour middle_data last_slack directory_name
    directory_attribute directory_creation directory_time directory_date directory_cluster directory_size directory_neighbour)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "changed_before_finish.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS before_open invalid_arguments no_time bad_calendar calendar_retry readonly_retry dirty_retry
    fat_mismatch_retry platform_retry wrong_shell legacy_exclusion empty_text readonly_file directory_failure command_sequence
    time_after_stage staged_close reset_retry release_retry late_clean_failure callback_reentry output_reentry)
    list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES "console.${UMICOM_CASE}")
endforeach()
list(APPEND UMICOM_FAT16_FILE_COMMIT_CASES read_budget.exact_boundary read_budget.one_over_reserve)
foreach(UMICOM_CASE IN LISTS UMICOM_FAT16_FILE_COMMIT_CASES)
    add_test(NAME "fat16-file-commit.${UMICOM_CASE}" COMMAND umicom-fat16-file-commit-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-file-commit.${UMICOM_CASE}" PROPERTIES TIMEOUT 60)
endforeach()

# The pure read-only planner has a separate executable and owns no transport,
# DMA frames or write callback. Every read-failure position is generated from a
# successful trace so additional validation reads remain covered automatically.
add_executable(umicom-fat16-file-plan-tests file_plan_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-file-plan-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-file-plan-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_FILE_COMMIT_SANITIZERS)
    target_compile_options(umicom-fat16-file-plan-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-file-plan-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_FAT16_FILE_PLAN_CASES all_read_failures path_result_boundary)
foreach(UMICOM_CASE IN ITEMS boundaries second_flooring invalid_fields overlap input_alignment output_alignment
    input_span_overflow output_span_overflow null_input null_output)
    list(APPEND UMICOM_FAT16_FILE_PLAN_CASES "calendar.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS root_archive_clear root_archive_set hidden_system nested second_root_sector fragmented_parent
    input_snapshot callback_reentry workspace_reuse)
    list(APPEND UMICOM_FAT16_FILE_PLAN_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS read_only old_archive_rule orphan long_name)
    list(APPEND UMICOM_FAT16_FILE_PLAN_CASES "refusal.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN ITEMS input_file_workspace input_file_plan time_file_workspace time_data_plan result_file_workspace
    workspace_overlap outputs_overlap time_input path_time path_file_plan unterminated_path invalid_time
    file_workspace_alignment file_plan_alignment time_alignment input_span_overflow file_plan_span_overflow
    time_span_overflow dirty_workspace copied_workspace copied_volume busy_workspace busy_volume null_time null_file_plan)
    list(APPEND UMICOM_FAT16_FILE_PLAN_CASES "guard.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN LISTS UMICOM_FAT16_FILE_PLAN_CASES)
    add_test(NAME "fat16-file-plan.${UMICOM_CASE}" COMMAND umicom-fat16-file-plan-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-file-plan.${UMICOM_CASE}" PROPERTIES TIMEOUT 60)
endforeach()
