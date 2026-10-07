# Umicom Kernel host qualification of the actual read-only driver and ownership.
# The model supplies register side effects and DMA completions, not a new driver.
# Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
cmake_minimum_required(VERSION 3.24)
project(UmicomReadOnlyBlockTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_BLOCK_SANITIZERS "Use address and undefined-behaviour sanitizers" ON)
add_executable(umicom-block-tests virtio_block_tests.c
    "${UMICOM_ROOT}/kernel/virtio_block.c"
    "${UMICOM_ROOT}/kernel/block_platform_policy.c"
    "${UMICOM_ROOT}/kernel/block_console.c"
    "${UMICOM_ROOT}/kernel/virtio_block_validation.c"
    "${UMICOM_ROOT}/kernel/physical_memory.c"
    "${UMICOM_ROOT}/kernel/address.c")
target_include_directories(umicom-block-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-block-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
# Failure injection wraps only allocation/release, leaving their real bitmap
# implementation linked. This optional native suite uses an ELF host linker.
target_link_options(umicom-block-tests PRIVATE
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
if(UMICOM_BLOCK_SANITIZERS)
    target_compile_options(umicom-block-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-block-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_BLOCK_CASES
    initialise invalid_initialise probe empty_slot legacy_transport bad_magic other_device
    missing_modern_feature writable_device features_rejected queue_absent queue_too_small
    queue_enable_refused driver_ok_refused zero_capacity unstable_capacity foreign_active_driver
    first_sector last_sector multi_sector bounds output_overlap
    used_id short_length long_length zero_length used_jump status_unwritten status_unknown
    device_needs_reset config_changed timeout stopped_clock backward_clock unsolicited_used
    io_error_retry unsupported_retry reset_retains_dma failed_open_retains_handle allocation_failures
    open_reset_retained delayed_completion_and_reset noncontiguous_dma invalid_entry
    memory_budgets release_retries stale_and_exhausted_generations exclusive_and_reentry unsafe_context
    full_index_wrap repeated_lifetimes high_capacity
    policy_valid policy_window policy_duplicate policy_span policy_untranslated policy_ram policy_hart
    policy_reservations policy_reserved_node policy_clock policy_count policy_not_ready policy_disabled
    console_probe console_sector console_retry guest_c_sequence guest_absent)
foreach(UMICOM_BLOCK_CASE IN LISTS UMICOM_BLOCK_CASES)
    add_test(NAME block.${UMICOM_BLOCK_CASE} COMMAND umicom-block-tests "${UMICOM_BLOCK_CASE}")
    set_tests_properties(block.${UMICOM_BLOCK_CASE} PROPERTIES TIMEOUT 60)
endforeach()

add_test(NAME block.fixture_bytes COMMAND umicom-block-tests fixture_bytes "${CMAKE_CURRENT_SOURCE_DIR}/fixture.raw")
