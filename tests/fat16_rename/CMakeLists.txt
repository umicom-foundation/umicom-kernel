# Umicom Kernel native qualification of bounded FAT16 rename commits.
# Real parser, updater, block transport and allocator; independent media model.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16RenameTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_RENAME_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-rename-tests rename_tests.c
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
target_include_directories(umicom-fat16-rename-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-rename-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-fat16-rename-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomPlatformBlockDomainGet
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_FAT16_RENAME_SANITIZERS)
    target_compile_options(umicom-fat16-rename-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-rename-tests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(umicom-fat16-rename-image-verify image_verify.c)
target_include_directories(umicom-fat16-rename-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-rename-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_RENAME_SANITIZERS)
    target_compile_options(umicom-fat16-rename-image-verify PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-rename-image-verify PRIVATE -fsanitize=address,undefined)
endif()

add_executable(umicom-fat16-rename-plan-tests rename_plan_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c" "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-rename-plan-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-rename-plan-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_RENAME_SANITIZERS)
    target_compile_options(umicom-fat16-rename-plan-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-rename-plan-tests PRIVATE -fsanitize=address,undefined)
endif()


set(UMICOM_RENAME_PLAN_CASES
    success.ordinary
    success.empty
    success.archive_clear
    success.hidden_system
    success.case_flags
    success.mixed_case
    success.no_extension
    success.maximum_alias
    success.short_extension
    success.different_parent
    success.deleted_collision
    success.root_last_record
    success.nested_fragmented
    success.snapshot
    success.reentry
    success.reuse
    refusal.same_alias
    refusal.case_only
    refusal.file_collision
    refusal.directory_collision
    refusal.root
    refusal.directory
    refusal.missing
    refusal.readonly
    refusal.allocated_empty
    refusal.orphan
    grammar.empty
    grammar.dot
    grammar.parent
    grammar.leading_dot
    grammar.trailing_dot
    grammar.multiple_dots
    grammar.base_long
    grammar.extension_long
    grammar.space
    grammar.slash
    grammar.backslash
    grammar.colon
    grammar.control
    grammar.high_byte
    grammar.unterminated
    grammar.relative_source
    grammar.parent_source
    grammar.unterminated_source
    guard.output_volume
    guard.output_base_workspace
    guard.output_workspace
    guard.workspace_volume
    guard.workspaces_overlap
    guard.path_volume
    guard.path_base_workspace
    guard.path_workspace
    guard.path_output
    guard.name_volume
    guard.name_base_workspace
    guard.name_workspace
    guard.name_output
    guard.strings_overlap
    guard.volume_alignment
    guard.workspace_alignment
    guard.output_alignment
    guard.output_overflow
    guard.workspace_overflow
    guard.null_volume
    guard.null_workspace
    guard.null_base_workspace
    guard.null_output
    guard.null_path
    guard.null_name
    guard.busy_volume
    guard.busy_base_workspace
    guard.busy_workspace
    guard.dirty_workspace
    guard.copied_workspace
    guard.copied_volume
    boundary.path_output
    boundary.name_output
    alphabet
    all_read_failures)
foreach(UMICOM_CASE IN LISTS UMICOM_RENAME_PLAN_CASES)
    add_test(NAME "fat16-rename.plan.${UMICOM_CASE}" COMMAND umicom-fat16-rename-plan-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-rename.plan.${UMICOM_CASE}" PROPERTIES TIMEOUT 90)
endforeach()

set(UMICOM_RENAME_TRANSPORT_CASES
    success.ordinary
    success.empty
    success.archive_set
    success.hidden_system
    success.nt_case_flags
    success.mixed_case
    success.casefold_source
    success.no_extension
    success.maximum_alias
    success.different_parent
    success.deleted_collision
    success.root_later_sector
    success.root_last_entry
    success.nested_first
    success.nested_fragmented
    success.nested_last_entry
    success.all_preserved_fields
    success.malformed_timestamp_preserved
    success.write_through
    success.callback_reentry
    success.policy_reentry
    success.idle_intervals
    refusal.same_alias
    refusal.case_only
    refusal.file_collision
    refusal.directory_collision
    refusal.root
    refusal.directory
    refusal.missing
    refusal.readonly
    refusal.invalid_name
    refusal.move_name
    refusal.trailing_dot
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
    ownership.result_plan
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
    ownership.busy_workspace
    ownership.busy_base_workspace
    ownership.busy_volume
    ownership.busy_domain
    ownership.unsafe_context
    ownership.path_owner
    ownership.path_plan
    ownership.path_domain
    ownership.path_dma
    ownership.path_queue
    ownership.path_result
    ownership.path_name
    ownership.path_unterminated
    ownership.path_null
    ownership.name_owner
    ownership.name_plan
    ownership.name_domain
    ownership.name_dma
    ownership.name_queue
    ownership.name_result
    ownership.name_path
    ownership.name_unterminated
    ownership.name_null
    finish_ownership.result_owner
    finish_ownership.result_plan
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
    finish_ownership.busy_workspace
    finish_ownership.busy_base_workspace
    finish_ownership.busy_volume
    finish_ownership.busy_domain
    finish_ownership.unsafe_context
    finish_ownership.unused_data_plan
    open_arguments.owner_null
    open_arguments.owner_alignment
    open_arguments.domain_alias
    open_arguments.dma_alias
    open_arguments.null_domain
    open_arguments.slot
    open_arguments.partition
    open_arguments.timeout_zero
    open_arguments.timeout_excess
    open_arguments.nonzero_storage
    failures.read_errors
    failures.read_mismatches
    failures.write_errors
    failures.flush_errors
    failures.dropped_writes
    failures.altered_writes
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
    tamper.primary_header
    tamper.mirror_header
    tamper.name
    tamper.attribute
    tamper.case_bits
    tamper.creation
    tamper.time
    tamper.date
    tamper.cluster
    tamper.size
    tamper.neighbour
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
    console.initial_get_file
    console.initial_get_rename
    console.foreign_rename_after_file
    console.foreign_file_after_rename
    console.foreign_append_after_rename
    console.foreign_rename_after_failed_file
    console.foreign_file_after_failed_rename
    console.wrong_shell
    console.collision_retry
    console.invalid_name_retry
    console.directory_failure
    console.sequence
    console.staged_close
    console.shell_shutdown
    console.stage_twice
    console.queue_reentry
    console.output_reentry
    console.late_clean_failure
    lifetime.zero_close
    lifetime.readonly_retry
    lifetime.no_flush_retry
    lifetime.failed_open_retained
    lifetime.ready_close
    lifetime.finish_before_stage
    lifetime.stage_twice
    lifetime.reset_retry
    lifetime.first_release_retry
    lifetime.second_release_retry
    lifetime.unsafe_close_retry
    lifetime.finish_twice
    lifetime.single_lifetime
    lifetime.staged_close
    lifetime.timeout_retained)
foreach(UMICOM_CASE IN LISTS UMICOM_RENAME_TRANSPORT_CASES)
    add_test(NAME "fat16-rename.${UMICOM_CASE}" COMMAND umicom-fat16-rename-tests "${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-rename.${UMICOM_CASE}" PROPERTIES TIMEOUT 90)
endforeach()
