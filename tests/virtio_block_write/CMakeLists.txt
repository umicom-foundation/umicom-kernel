# Umicom Kernel native qualification of explicit WRITE/FLUSH leases.
# This target reuses the real driver, allocator and original transport model.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomWritableBlockTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_BLOCK_WRITE_SANITIZERS "Use address and undefined-behaviour sanitizers" ON)
add_executable(umicom-block-write-tests write_tests.c
    "${UMICOM_ROOT}/kernel/virtio_block.c"
    "${UMICOM_ROOT}/kernel/block_platform_policy.c"
    "${UMICOM_ROOT}/kernel/block_console.c"
    "${UMICOM_ROOT}/kernel/virtio_block_validation.c"
    "${UMICOM_ROOT}/kernel/physical_memory.c"
    "${UMICOM_ROOT}/kernel/address.c")
target_include_directories(umicom-block-write-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-block-write-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
target_link_options(umicom-block-write-tests PRIVATE
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_BLOCK_WRITE_SANITIZERS)
    target_compile_options(umicom-block-write-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-block-write-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_BLOCK_WRITE_CASES
    admission readonly_boundary missing_flush missing_modern refused_features foreign_driver
    failed_open_retained bounds source_ownership outcome_ownership unsafe_and_reentry stale_and_exclusive
    single_sector last_sector multi_sector noncontiguous_dma owned_input_snapshot readback_and_flush
    publication_before_notify partial_io_error partial_unsupported preflight_faults flush_errors
    late_completion_and_reset release_retries cleanup_does_not_flush empty_flush mixed_ring_wrap allocation_failures
    final_completion_deadline final_completion_clock_rollback)
foreach(UMICOM_BLOCK_WRITE_FAULT IN ITEMS
    timeout stopped_clock backward_clock used_id used_zero used_long used_jump
    status_unwritten status_unknown configuration_change device_reset)
    list(APPEND UMICOM_BLOCK_WRITE_CASES "write_fault_${UMICOM_BLOCK_WRITE_FAULT}")
endforeach()
foreach(UMICOM_BLOCK_FLUSH_FAULT IN ITEMS timeout used_id used_zero used_long used_jump status_unwritten status_unknown)
    list(APPEND UMICOM_BLOCK_WRITE_CASES "flush_fault_${UMICOM_BLOCK_FLUSH_FAULT}")
endforeach()
foreach(UMICOM_BLOCK_WRITE_CASE IN LISTS UMICOM_BLOCK_WRITE_CASES)
    add_test(NAME block-write.${UMICOM_BLOCK_WRITE_CASE}
        COMMAND umicom-block-write-tests "${UMICOM_BLOCK_WRITE_CASE}")
    set_tests_properties(block-write.${UMICOM_BLOCK_WRITE_CASE} PROPERTIES TIMEOUT 60)
endforeach()
