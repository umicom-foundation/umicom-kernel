# Umicom Kernel native qualification of bounded FAT16 append commits.
# Real parser, updater, block transport and allocator; independent media model.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16AppendTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_APPEND_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-append-tests append_tests.c
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
target_include_directories(umicom-fat16-append-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-append-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-fat16-append-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_FAT16_APPEND_SANITIZERS)
    target_compile_options(umicom-fat16-append-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-append-tests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(umicom-fat16-append-image-verify image_verify.c)
target_include_directories(umicom-fat16-append-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-append-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_APPEND_SANITIZERS)
    target_compile_options(umicom-fat16-append-image-verify PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-append-image-verify PRIVATE -fsanitize=address,undefined)
endif()

add_executable(umicom-fat16-append-plan-tests append_plan_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c" "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-append-plan-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-append-plan-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_APPEND_SANITIZERS)
    target_compile_options(umicom-fat16-append-plan-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-append-plan-tests PRIVATE -fsanitize=address,undefined)
endif()

set(UMICOM_APPEND_PLAN_CASES
    success.ordinary
    success.archive_clear
    success.hidden_system
    success.single_byte
    success.exact_capacity
    success.last_byte
    success.casefold
    success.last_root_record
    success.nested_fragmented
    success.input_snapshot
    success.callback_reentry
    success.workspace_reuse
    range.one_over_capacity
    range.full_cluster
    range.empty_file
    range.size_overflow
    range.zero_bytes
    range.oversized
    range.directory
    range.missing
    range.read_only
    guard.input_file_workspace
    guard.input_file_plan
    guard.time_file_workspace
    guard.time_data_plan
    guard.result_file_workspace
    guard.workspace_overlap
    guard.outputs_overlap
    guard.time_input
    guard.path_time
    guard.path_file_plan
    guard.unterminated_path
    guard.invalid_time
    guard.file_workspace_alignment
    guard.file_plan_alignment
    guard.time_alignment
    guard.input_span_overflow
    guard.file_plan_span_overflow
    guard.time_span_overflow
    guard.dirty_workspace
    guard.copied_workspace
    guard.copied_volume
    guard.busy_workspace
    guard.busy_volume
    guard.null_time
    guard.null_file_plan
    refusal.read_only
    refusal.orphan
    refusal.long_name
    all_read_failures
    path_result_boundary)
foreach(UMICOM_CASE IN LISTS UMICOM_APPEND_PLAN_CASES)
    add_test(NAME "fat16-append.plan.${UMICOM_CASE}" COMMAND umicom-fat16-append-plan-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-append.plan.${UMICOM_CASE}" PROPERTIES TIMEOUT 90)
endforeach()

set(UMICOM_APPEND_TRANSPORT_CASES
    success.ordinary
    success.archive_set
    success.hidden_system
    success.single_byte
    success.exact_capacity
    success.last_byte
    success.write_through
    success.root_later_sector
    success.root_last_entry
    success.nested_first
    success.nested_fragmented
    success.nested_last_entry
    success.preserved_fields
    success.casefold
    success.large_maximum
    success.large_exact_capacity
    success.large_aligned
    success.callback_reentry
    success.policy_reentry
    success.idle_intervals
    range.one_over_capacity
    range.full_cluster
    range.empty_file
    range.size_overflow
    range.directory
    range.root
    range.missing
    range.readonly
    range.large_one_over_capacity
    format.file_crosslink
    format.directory_crosslink
    format.shared_tail
    format.orphan_to_target
    format.orphan_allocation
    format.allocated_empty
    format.directory_cycle
    format.wrong_parent
    format.missing_dot
    format.missing_parent
    format.root_label_cluster
    format.nested_label
    format.live_lfn
    format.bad_cluster
    format.free_link
    format.reserved_link
    format.target_cycle
    format.short_chain
    format.surplus_chain
    format.dirty
    format.hard_error
    format.padding_mismatch
    format.remote_fat_mismatch
    format.unknown_attribute
    format.readonly
    format.duplicate_name
    format.directory_limit
    format.object_limit
    format.depth_limit
    format.chain_limit
    format.io_budget
    ownership.result_owner
    ownership.result_outer_plan
    ownership.result_domain
    ownership.result_dma
    ownership.result_queue
    ownership.result_alignment
    ownership.result_overflow
    ownership.result_null
    ownership.other_slot_dma
    ownership.owner_alignment
    ownership.owner_null
    ownership.copied_owner
    ownership.busy_owner
    ownership.busy_embedded
    ownership.busy_updater
    ownership.busy_file_workspace
    ownership.busy_base_workspace
    ownership.busy_volume
    ownership.busy_domain
    ownership.unsafe_context
    ownership.input_owner
    ownership.input_outer_plan
    ownership.input_domain
    ownership.input_dma
    ownership.input_queue
    ownership.input_result
    ownership.input_time
    ownership.input_overflow
    ownership.input_null
    ownership.input_zero
    ownership.input_oversized
    ownership.path_input
    ownership.path_input_nonterminated
    ownership.path_result_nonterminated
    ownership.path_time
    ownership.path_outer_plan
    ownership.path_domain
    ownership.path_dma
    ownership.path_unterminated
    ownership.path_null
    ownership.time_owner
    ownership.time_outer_plan
    ownership.time_domain
    ownership.time_dma
    ownership.time_queue
    ownership.time_result
    ownership.time_alignment
    ownership.time_overflow
    ownership.time_null
    finish_ownership.result_owner
    finish_ownership.result_outer_plan
    finish_ownership.result_domain
    finish_ownership.result_dma
    finish_ownership.result_queue
    finish_ownership.result_alignment
    finish_ownership.result_overflow
    finish_ownership.result_null
    finish_ownership.other_slot_dma
    finish_ownership.owner_alignment
    finish_ownership.owner_null
    finish_ownership.copied_owner
    finish_ownership.busy_owner
    finish_ownership.busy_embedded
    finish_ownership.busy_updater
    finish_ownership.busy_file_workspace
    finish_ownership.busy_base_workspace
    finish_ownership.busy_volume
    finish_ownership.busy_domain
    finish_ownership.unsafe_context
    failures.read_errors
    failures.read_errors_large
    failures.read_mismatches_large
    failures.write_errors_large
    failures.flush_errors_large
    failures.dropped_writes_large
    failures.altered_writes_large
    cut.cached.1
    cut.cached.2
    cut.cached.3
    cut.cached.4
    cut.cached.5
    cut.cached.6
    cut.cached.7
    cut.cached.8
    cut.cached.9
    cut.cached.10
    cut.cached.11
    cut.cached.12
    cut.cached.13
    cut.cached.14
    cut.cached.15
    cut.cached.16
    cut.cached.17
    cut.cached.18
    cut.cached.19
    cut.cached.20
    cut.eager.1
    cut.eager.2
    cut.eager.3
    cut.eager.4
    cut.eager.5
    cut.eager.6
    cut.eager.7
    cut.eager.8
    cut.eager.9
    cut.eager.10
    cut.eager.11
    cut.eager.12
    cut.eager.13
    cut.eager.14
    cut.eager.15
    cut.eager.16
    cut.eager.17
    cut.eager.18
    cut.eager.19
    cut.eager.20
    tamper.primary_header
    tamper.mirror_header
    tamper.old_prefix
    tamper.appended_data
    tamper.remaining_slack
    tamper.directory_name
    tamper.directory_attribute
    tamper.directory_creation
    tamper.directory_time
    tamper.directory_date
    tamper.directory_cluster
    tamper.directory_size
    tamper.directory_neighbour
    clock.outer_deadline
    clock.outer_rollback
    clock.inner_deadline
    clock.inner_rollback
    acceptance_clock.stage_deadline
    acceptance_clock.stage_rollback
    acceptance_clock.finish_deadline
    acceptance_clock.finish_rollback
    preflight.directory_mismatch
    preflight.last_clock_deadline
    preflight.last_clock_rollback
    console.before_open
    console.invalid_arguments
    console.no_time
    console.empty_text
    console.wrong_shell
    console.range_refusal
    console.append_after_overwrite
    console.directory_failure
    console.sequence
    console.staged_close
    console.queue_reentry
    console.output_reentry
    console.late_clean_failure
    lifetime.ready_close
    lifetime.finish_before_append
    lifetime.append_twice
    lifetime.overwrite_after_append
    lifetime.reset_retry
    lifetime.first_release_retry
    lifetime.second_release_retry
    lifetime.staged_close)
foreach(UMICOM_CASE IN LISTS UMICOM_APPEND_TRANSPORT_CASES)
    add_test(NAME "fat16-append.${UMICOM_CASE}" COMMAND umicom-fat16-append-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-append.${UMICOM_CASE}" PROPERTIES TIMEOUT 90)
endforeach()
