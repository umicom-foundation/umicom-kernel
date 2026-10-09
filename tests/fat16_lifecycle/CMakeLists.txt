# Umicom Kernel native qualification of bounded FAT16 file lifecycle commits.
# Real parser, updater, block transport and allocator; independent media model.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16LifecycleTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_FAT16_LIFECYCLE_SANITIZERS "Enable address and undefined-behaviour sanitizers" ON)
add_executable(umicom-fat16-lifecycle-tests lifecycle_tests.c
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
target_include_directories(umicom-fat16-lifecycle-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-lifecycle-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-fat16-lifecycle-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomPlatformBlockDomainGet
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_FAT16_LIFECYCLE_SANITIZERS)
    target_compile_options(umicom-fat16-lifecycle-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-lifecycle-tests PRIVATE -fsanitize=address,undefined)
endif()
add_executable(umicom-fat16-lifecycle-image-verify image_verify.c)
target_include_directories(umicom-fat16-lifecycle-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-lifecycle-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_LIFECYCLE_SANITIZERS)
    target_compile_options(umicom-fat16-lifecycle-image-verify PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-lifecycle-image-verify PRIVATE -fsanitize=address,undefined)
endif()

add_executable(umicom-fat16-lifecycle-plan-tests lifecycle_plan_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c" "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-lifecycle-plan-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-lifecycle-plan-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_FAT16_LIFECYCLE_SANITIZERS)
    target_compile_options(umicom-fat16-lifecycle-plan-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-fat16-lifecycle-plan-tests PRIVATE -fsanitize=address,undefined)
endif()


set(UMICOM_LIFECYCLE_PLAN_CASES
    create.success.ordinary
    create.success.empty
    create.success.one
    create.success.maximum
    create.success.sector
    create.success.mixed_case
    create.success.deleted_slot
    create.success.nested
    create.success.end_boundary
    create.success.fragmented_boundary
    create.success.fat_crossing
    create.success.fat_later_crossing
    create.success.snapshot
    create.success.reentry
    append.success.ordinary
    append.success.empty
    append.success.one
    append.success.maximum
    append.success.sector
    append.success.partial
    append.success.preserved_fields
    append.success.nested
    append.success.fat_crossing
    append.success.fat_later_crossing
    append.success.snapshot
    append.success.reentry
    truncate.success.ordinary
    truncate.success.empty
    truncate.success.zero
    truncate.success.equal
    truncate.success.cluster_boundary
    truncate.success.preserved_fields
    truncate.success.nested
    truncate.success.spread_chain
    truncate.success.fat_limit
    truncate.success.snapshot
    truncate.success.reentry
    delete.success.ordinary
    delete.success.empty
    delete.success.preserved_fields
    delete.success.nested
    delete.success.spread_chain
    delete.success.fat_limit
    delete.success.snapshot
    delete.success.reentry
    create.refusal.exists
    create.refusal.root
    create.refusal.directory
    create.refusal.invalid_time
    create.refusal.too_many_bytes
    create.refusal.unused_size
    create.refusal.null_input
    create.refusal.none
    create.refusal.bad_operation
    create.refusal.relative
    create.refusal.parent_path
    # The old fixed-parent expectation remains in the test source for review.
    # create.refusal.full_parent
    create.success.parent_growth
    create.refusal.no_clusters
    create.refusal.io_budget
    create.refusal.orphan
    create.refusal.shared_tail
    create.refusal.allocated_empty
    create.refusal.surplus_chain
    append.refusal.missing
    append.refusal.root
    append.refusal.directory
    append.refusal.readonly
    append.refusal.invalid_time
    append.refusal.zero_append
    append.refusal.too_many_bytes
    append.refusal.unused_size
    append.refusal.null_input
    append.refusal.no_clusters
    append.refusal.orphan
    append.refusal.shared_tail
    append.refusal.allocated_empty
    append.refusal.surplus_chain
    truncate.refusal.missing
    truncate.refusal.root
    truncate.refusal.directory
    truncate.refusal.readonly
    truncate.refusal.invalid_time
    truncate.refusal.grow_truncate
    truncate.refusal.unused_input
    truncate.refusal.fat_limit
    delete.refusal.missing
    delete.refusal.root
    delete.refusal.directory
    delete.refusal.readonly
    delete.refusal.unexpected_time
    delete.refusal.unused_size
    delete.refusal.unused_input
    delete.refusal.fat_limit
    create.guard.null_volume
    create.guard.null_request
    create.guard.null_base
    create.guard.null_workspace
    create.guard.null_output
    create.guard.volume_alignment
    create.guard.request_alignment
    create.guard.workspace_alignment
    create.guard.output_alignment
    create.guard.output_overflow
    create.guard.output_volume
    create.guard.output_workspace
    create.guard.workspaces_overlap
    create.guard.request_workspace
    create.guard.request_output
    create.guard.path_output
    create.guard.path_workspace
    create.guard.path_request
    create.guard.path_input
    create.guard.input_output
    create.guard.input_workspace
    create.guard.input_request
    create.guard.input_path
    create.guard.busy_volume
    create.guard.busy_base
    create.guard.busy_workspace
    create.guard.copied_volume
    create.guard.copied_workspace
    create.guard.dirty_workspace
    create.guard.unterminated_path
    create.all_read_failures
    append.all_read_failures
    truncate.all_read_failures
    delete.all_read_failures
)
foreach(UMICOM_CASE IN LISTS UMICOM_LIFECYCLE_PLAN_CASES)
    add_test(NAME "fat16-lifecycle.plan.${UMICOM_CASE}" COMMAND umicom-fat16-lifecycle-plan-tests "${UMICOM_CASE}" "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-lifecycle.plan.${UMICOM_CASE}" PROPERTIES TIMEOUT 120)
endforeach()

set(UMICOM_LIFECYCLE_TRANSPORT_CASES
    create.fault.fragmented_boundary.read_errors
    create.fault.fragmented_boundary.read_mismatches
    create.fault.fragmented_boundary.write_errors
    create.fault.fragmented_boundary.flush_errors
    create.fault.fragmented_boundary.dropped_writes
    create.fault.fragmented_boundary.altered_writes
    truncate.fault.spread_chain.read_errors
    truncate.fault.spread_chain.read_mismatches
    truncate.fault.spread_chain.write_errors
    truncate.fault.spread_chain.flush_errors
    truncate.fault.spread_chain.dropped_writes
    truncate.fault.spread_chain.altered_writes
    create.geometry.two_sector_initialisation
    create.geometry.maximum_initialisation
    create.geometry.suffix_before_submit
    create.geometry.suffix_write_failure
    create.console.arguments
    create.console.initial_get
    create.console.missing_time
    create.console.calendar
    create.console.sequence
    create.console.queue_reentry
    create.console.output_reentry
    create.console.write_failure
    create.console.staged_close
    create.console.shutdown
    create.console.stage_twice
    create.console.foreign_lifecycle_after_file
    create.console.foreign_lifecycle_after_rename
    create.console.foreign_file_after_lifecycle
    create.console.foreign_rename_after_lifecycle
    create.console.foreign_lifecycle_after_file_failed
    create.console.foreign_file_after_lifecycle_failed
    create.success.ordinary
    create.success.empty
    create.success.one
    create.success.maximum
    create.success.sector
    create.success.mixed_case
    create.success.deleted_slot
    create.success.nested
    create.success.end_boundary
    create.success.fragmented_boundary
    create.success.fat_crossing
    create.success.fat_later_crossing
    create.success.snapshot
    create.success.reentry
    append.success.ordinary
    append.success.empty
    append.success.one
    append.success.maximum
    append.success.sector
    append.success.partial
    append.success.preserved_fields
    append.success.nested
    append.success.fat_crossing
    append.success.fat_later_crossing
    append.success.snapshot
    append.success.reentry
    truncate.success.ordinary
    truncate.success.empty
    truncate.success.zero
    truncate.success.equal
    truncate.success.cluster_boundary
    truncate.success.preserved_fields
    truncate.success.nested
    truncate.success.spread_chain
    truncate.success.fat_limit
    truncate.success.snapshot
    truncate.success.reentry
    delete.success.ordinary
    delete.success.empty
    delete.success.preserved_fields
    delete.success.nested
    delete.success.spread_chain
    delete.success.fat_limit
    delete.success.snapshot
    delete.success.reentry
    create.success.eager
    create.success.policy_reentry
    append.success.eager
    append.success.policy_reentry
    truncate.success.eager
    truncate.success.policy_reentry
    delete.success.eager
    delete.success.policy_reentry
    create.fault.read_errors
    create.fault.read_mismatches
    create.fault.write_errors
    create.fault.flush_errors
    create.fault.dropped_writes
    create.fault.altered_writes
    append.fault.read_errors
    append.fault.read_mismatches
    append.fault.write_errors
    append.fault.flush_errors
    append.fault.dropped_writes
    append.fault.altered_writes
    truncate.fault.read_errors
    truncate.fault.read_mismatches
    truncate.fault.write_errors
    truncate.fault.flush_errors
    truncate.fault.dropped_writes
    truncate.fault.altered_writes
    delete.fault.read_errors
    delete.fault.read_mismatches
    delete.fault.write_errors
    delete.fault.flush_errors
    delete.fault.dropped_writes
    delete.fault.altered_writes
    create.cuts.cached
    create.cuts.eager
    append.cuts.cached
    append.cuts.eager
    truncate.cuts.cached
    truncate.cuts.eager
    delete.cuts.cached
    delete.cuts.eager
    create.guard.result_owner
    create.guard.result_plan
    create.guard.result_domain
    create.guard.result_dma
    create.guard.result_queue
    create.guard.result_alignment
    create.guard.result_overflow
    create.guard.result_null
    create.guard.result_request
    create.guard.owner_null
    create.guard.owner_alignment
    create.guard.copied_owner
    create.guard.request_owner
    create.guard.request_domain
    create.guard.request_result
    create.guard.request_dma
    create.guard.request_null
    create.guard.request_alignment
    create.guard.path_owner
    create.guard.path_result
    create.guard.path_request
    create.guard.path_dma
    create.guard.path_domain
    create.guard.path_input
    create.guard.input_owner
    create.guard.input_result
    create.guard.input_request
    create.guard.input_dma
    create.guard.input_path
    create.guard.busy_owner
    create.guard.busy_embedded
    create.guard.busy_updater
    create.guard.busy_workspace
    create.guard.busy_base
    create.guard.busy_volume
    create.guard.busy_domain
    create.guard.unsafe_context
    create.finish_guard.result_owner
    create.finish_guard.result_plan
    create.finish_guard.result_domain
    create.finish_guard.result_dma
    create.finish_guard.result_queue
    create.finish_guard.result_alignment
    create.finish_guard.result_overflow
    create.finish_guard.result_null
    create.finish_guard.owner_null
    create.finish_guard.owner_alignment
    create.finish_guard.copied_owner
    create.finish_guard.busy_owner
    create.finish_guard.busy_embedded
    create.finish_guard.busy_updater
    create.finish_guard.busy_workspace
    create.finish_guard.busy_base
    create.finish_guard.busy_volume
    create.finish_guard.busy_domain
    create.finish_guard.unsafe_context
    create.lifetime.zero_close
    create.lifetime.readonly_retry
    create.lifetime.no_flush_retry
    create.lifetime.failed_open_retained
    create.lifetime.ready_close
    create.lifetime.finish_before_stage
    create.lifetime.timeout_retained
    create.lifetime.stage_twice
    create.lifetime.reset_retry
    create.lifetime.first_release_retry
    create.lifetime.second_release_retry
    create.lifetime.unsafe_close_retry
    create.lifetime.finish_twice
    create.lifetime.single_lifetime
    create.tamper.primary_header
    create.tamper.mirror_header
    create.tamper.name
    create.tamper.attribute
    create.tamper.time
    create.tamper.cluster
    create.tamper.size
    create.tamper.neighbour
    append.tamper.primary_header
    append.tamper.mirror_header
    append.tamper.name
    append.tamper.attribute
    append.tamper.time
    append.tamper.cluster
    append.tamper.size
    append.tamper.neighbour
    truncate.tamper.primary_header
    truncate.tamper.mirror_header
    truncate.tamper.name
    truncate.tamper.attribute
    truncate.tamper.time
    truncate.tamper.cluster
    truncate.tamper.size
    truncate.tamper.neighbour
    delete.tamper.primary_header
    delete.tamper.mirror_header
    delete.tamper.name
    delete.tamper.attribute
    delete.tamper.time
    delete.tamper.cluster
    delete.tamper.size
    delete.tamper.neighbour
    create.tamper.data
    create.tamper.zero_slack
    append.tamper.data
    append.tamper.zero_slack
    truncate.tamper.later_fat_primary
    truncate.tamper.later_fat_mirror
    delete.tamper.later_fat_primary
    delete.tamper.later_fat_mirror
    create.format.file_crosslink
    create.format.directory_crosslink
    create.format.shared_tail
    create.format.orphan_to_target
    create.format.orphan_allocation
    create.format.allocated_empty
    create.format.directory_cycle
    create.format.wrong_parent
    create.format.missing_dot
    create.format.missing_parent
    create.format.root_label_cluster
    create.format.nested_label
    create.format.live_lfn
    create.format.bad_cluster
    create.format.free_link
    create.format.reserved_link
    create.format.target_cycle
    create.format.short_chain
    create.format.surplus_chain
    create.format.padding_mismatch
    create.format.remote_fat_mismatch
    create.format.unknown_attribute
    create.format.duplicate_name
    create.format.directory_limit
    create.format.object_limit
    create.format.depth_limit
    create.format.chain_limit
    create.format.io_budget
    create.session.ordinary
    create.session.empty
    create.session.retry_preflight
    create.clock.inner_deadline
    create.clock.inner_rollback
    create.clock.outer_deadline
    create.clock.outer_rollback
    create.clock.accept_stage_deadline
    create.clock.accept_stage_rollback
    create.clock.accept_finish_deadline
    create.clock.accept_finish_rollback
    append.clock.inner_deadline
    append.clock.inner_rollback
    append.clock.outer_deadline
    append.clock.outer_rollback
    append.clock.accept_stage_deadline
    append.clock.accept_stage_rollback
    append.clock.accept_finish_deadline
    append.clock.accept_finish_rollback
    truncate.clock.inner_deadline
    truncate.clock.inner_rollback
    truncate.clock.outer_deadline
    truncate.clock.outer_rollback
    truncate.clock.accept_stage_deadline
    truncate.clock.accept_stage_rollback
    truncate.clock.accept_finish_deadline
    truncate.clock.accept_finish_rollback
    delete.clock.inner_deadline
    delete.clock.inner_rollback
    delete.clock.outer_deadline
    delete.clock.outer_rollback
    delete.clock.accept_stage_deadline
    delete.clock.accept_stage_rollback
    delete.clock.accept_finish_deadline
    delete.clock.accept_finish_rollback
)
foreach(UMICOM_CASE IN LISTS UMICOM_LIFECYCLE_TRANSPORT_CASES)
    add_test(NAME "fat16-lifecycle.transport.${UMICOM_CASE}" COMMAND umicom-fat16-lifecycle-tests "${UMICOM_CASE}" "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-lifecycle.transport.${UMICOM_CASE}" PROPERTIES TIMEOUT 180)
endforeach()

# Directory cases use the same real transport with a separate raw-media oracle.
set(UMICOM_DIRECTORY_CASES
    move.rename-directory
    move.rename-file
    move.move-file
    move.move-empty
    move.move-directory
    move.to-root
    move.boundary
    move.console
    move.snapshot
    move.full-parent-rename
    move.depth.empty
    move.depth.leaf
    move.refusal.cycle
    move.refusal.root
    move.refusal.collision
    move.refusal.same-name
    move.refusal.missing-parent
    move.refusal.readonly-source
    move.refusal.readonly-destination
    move.refusal.readonly-parent
    move.refusal.full
    move.guard.null
    move.guard.owner
    move.guard.output
    move.guard.request
    move.guard.source
    move.guard.domain
    move.guard.unterminated
    move.guard.calendar
    move.guard.payload
    move.move-directory.fault.read
    move.move-directory.fault.write
    move.move-directory.fault.flush
    move.move-directory.fault.drop
    move.move-directory.fault.alter
    move.move-directory.fault.cut-cached
    move.move-directory.fault.cut-eager
    move.boundary.fault.read
    move.boundary.fault.write
    move.boundary.fault.flush
    move.boundary.fault.drop
    move.boundary.fault.alter
    move.boundary.fault.cut-cached
    move.boundary.fault.cut-eager
    query.guard.null-path
    query.guard.request-alignment
    query.guard.output-alignment
    query.guard.output-domain
    query.callbacks.reentry
    query.callbacks.timeout
    query.callbacks.rollback
    query.success.stat
    query.success.list
    query.success.read
    query.success.eof
    query.guard.null-request
    query.guard.null-output
    query.guard.request-owner
    query.guard.output-owner
    query.guard.request-output
    query.guard.path-owner
    query.guard.path-output
    query.guard.path-request
    query.guard.unterminated
    query.guard.kind
    query.guard.stat-offset
    query.guard.stat-capacity
    query.guard.read-zero
    query.guard.read-overflow
    query.guard.copied
    query.guard.busy
    query.guard.failed
    query.refusal.missing
    query.refusal.file-list
    query.refusal.directory-read
    query.failures.stat
    query.failures.list
    query.failures.read
    query.staged
    query.console
    geometry.two-sector
    geometry.scan-boundary
    geometry.beyond-scan-bound
    capacity.fixed-root
    capacity.growth-allocation-bound
    capacity.no-free-cluster
    arguments.mkdir-input
    arguments.mkdir-bytes
    arguments.mkdir-size
    arguments.mkdir-time
    arguments.remove-input
    arguments.remove-bytes
    arguments.remove-size
    arguments.remove-time
    collision.existing-file
    collision.existing-directory
    collision.missing-parent
    collision.file-parent
    collision.readonly-parent
    mkdir.growth.fragmented
    mkdir.growth.fragmented.fault.read
    mkdir.growth.fragmented.fault.write
    mkdir.growth.fragmented.fault.flush
    mkdir.growth.fragmented.fault.drop
    mkdir.growth.fragmented.fault.alter
    mkdir.growth.fragmented.fault.cut-cached
    mkdir.growth.fragmented.fault.cut-eager
    mkdir.root
    mkdir.nested
    mkdir.growth
    remove.root
    remove.nested
    remove.chain
    sequence
    console
    refusal.nonempty
    refusal.root
    refusal.file
    refusal.readonly
    refusal.bad-dot
    refusal.bad-parent
    refusal.missing
    mkdir.root.fault.read
    mkdir.root.fault.write
    mkdir.root.fault.flush
    mkdir.root.fault.drop
    mkdir.root.fault.alter
    mkdir.root.fault.cut-cached
    mkdir.root.fault.cut-eager
    mkdir.growth.fault.read
    mkdir.growth.fault.write
    mkdir.growth.fault.flush
    mkdir.growth.fault.drop
    mkdir.growth.fault.alter
    mkdir.growth.fault.cut-cached
    mkdir.growth.fault.cut-eager
    remove.chain.fault.read
    remove.chain.fault.write
    remove.chain.fault.flush
    remove.chain.fault.drop
    remove.chain.fault.alter
    remove.chain.fault.cut-cached
    remove.chain.fault.cut-eager)
foreach(UMICOM_CASE IN LISTS UMICOM_DIRECTORY_CASES)
    add_test(NAME "fat16-directory.${UMICOM_CASE}"
        COMMAND umicom-fat16-lifecycle-tests "directory.${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../fat16_file_commit/fixture.raw")
    set_tests_properties("fat16-directory.${UMICOM_CASE}" PROPERTIES TIMEOUT 180)
endforeach()

# A separate native executable checks host-generated checkpoint images using
# the guest's independent byte oracle before QEMU ever mutates a disposable copy.
add_executable(umicom-fat16-directory-image-verify ../fat16_directories/image_verify.c)
target_include_directories(umicom-fat16-directory-image-verify PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-fat16-directory-image-verify PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

# The read-only planner must reject overlapping destination storage before I/O.
foreach(UMICOM_MOVE_PLAN_CASE IN ITEMS success read-failures null volume workspace scratch output request source unterminated)
    add_test(NAME "fat16-lifecycle.plan.move.${UMICOM_MOVE_PLAN_CASE}"
        COMMAND umicom-fat16-lifecycle-plan-tests "move.${UMICOM_MOVE_PLAN_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-lifecycle.plan.move.${UMICOM_MOVE_PLAN_CASE}" PROPERTIES TIMEOUT 180)
endforeach()

# Positional writes keep their own coverage list so the unchanged lifecycle
# cases remain individually selectable and their historical names stay stable.
set(UMICOM_WRITE_SUCCESS_CASES
    overwrite beginning fragmented whole-existing eof-slack extension empty
    maximum preserved-fields nested fat-crossing full-volume-overwrite
    append-ignores-offset snapshot reentry)
set(UMICOM_WRITE_PLAN_CASES all-read-failures
    refusal.hole refusal.offset-overflow refusal.size-overflow
    refusal.readonly refusal.invalid_time refusal.zero_append
    refusal.too_many_bytes refusal.unused_size refusal.null_input
    refusal.no_clusters refusal.missing refusal.directory
    guard.input_request guard.path_input guard.output_workspace guard.request_alignment)
foreach(UMICOM_CASE IN LISTS UMICOM_WRITE_SUCCESS_CASES)
    list(APPEND UMICOM_WRITE_PLAN_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN LISTS UMICOM_WRITE_PLAN_CASES)
    add_test(NAME "fat16-lifecycle.plan.write.${UMICOM_CASE}"
        COMMAND umicom-fat16-lifecycle-plan-tests "write.${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-lifecycle.plan.write.${UMICOM_CASE}" PROPERTIES TIMEOUT 180)
endforeach()

set(UMICOM_WRITE_TRANSPORT_CASES
    success.eager success.policy-reentry sequence
    refusal.hole refusal.offset-overflow refusal.size-overflow
    refusal.readonly refusal.no-space refusal.missing refusal.directory
    refusal.calendar refusal.zero refusal.too-many refusal.unused-size refusal.null-input
    fault.read_errors fault.read_mismatches fault.write_errors fault.flush_errors
    fault.dropped_writes fault.altered_writes cuts.cached cuts.eager
    fault.write_overwrite.read_errors fault.write_overwrite.read_mismatches
    fault.write_overwrite.write_errors fault.write_overwrite.flush_errors
    fault.write_overwrite.dropped_writes fault.write_overwrite.altered_writes
    guard.input_request guard.path_input guard.result_owner guard.request_alignment
    finish-guard.result_owner finish-guard.result_alignment
    clock.accept_stage_deadline clock.accept_stage_rollback
    clock.accept_finish_deadline clock.accept_finish_rollback
    geometry.two-sector-initialisation geometry.maximum-initialisation
    geometry.suffix-before-submit geometry.suffix-write-failure)
foreach(UMICOM_CASE IN LISTS UMICOM_WRITE_SUCCESS_CASES)
    list(APPEND UMICOM_WRITE_TRANSPORT_CASES "success.${UMICOM_CASE}")
endforeach()
foreach(UMICOM_CASE IN LISTS UMICOM_WRITE_TRANSPORT_CASES)
    add_test(NAME "fat16-lifecycle.transport.write.${UMICOM_CASE}"
        COMMAND umicom-fat16-lifecycle-tests "write.${UMICOM_CASE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("fat16-lifecycle.transport.write.${UMICOM_CASE}" PROPERTIES TIMEOUT 180)
endforeach()
