#-----------------------------------------------------------------------------
# Umicom Kernel
# File: tests/fat16_space/CMakeLists.txt
#
# PURPOSE:
#   Build focused allocation inspection contracts against real Kernel sources.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
#-----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.24)
project(UmicomFat16SpaceTests LANGUAGES C)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(umicom-fat16-space-tests space_tests.c
    "${UMICOM_ROOT}/kernel/fat16_inspector.c"
    "${UMICOM_ROOT}/kernel/disk_partitions.c")
target_include_directories(umicom-fat16-space-tests PRIVATE "${UMICOM_ROOT}/include")
set_target_properties(umicom-fat16-space-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(umicom-fat16-space-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()
set(UMICOM_SPACE_CASES ordinary bad_cluster orphan_not_ownership cycle_not_ownership
    full empty last_cluster padding_ignored padding_mirror all_read_failures
    cache_invalidated callback_reentry reserved_link one_link outside_link
    dirty io_error_flag bad_flags bad_media)
foreach(field IN ITEMS clusters cluster_bytes fat_limit fat_zero data_start partition reader)
    list(APPEND UMICOM_SPACE_CASES "geometry.${field}")
endforeach()
foreach(field IN ITEMS null_owner null_result owner_alignment result_alignment owner_overflow
    result_overflow overlap copied busy closed)
    list(APPEND UMICOM_SPACE_CASES "guard.${field}")
endforeach()
foreach(case IN LISTS UMICOM_SPACE_CASES)
    add_test(NAME "kernel.fat16_space.${case}" COMMAND umicom-fat16-space-tests
        "${case}" "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("kernel.fat16_space.${case}" PROPERTIES TIMEOUT 30)
endforeach()

# Reuse the real driver and the established register/DMA model. The executable
# entry wrapper selects the new suite while preserving the included regressions.
add_executable(umicom-fat16-space-transport-tests transport_tests.c
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
target_include_directories(umicom-fat16-space-transport-tests PRIVATE "${UMICOM_ROOT}/include")
set_target_properties(umicom-fat16-space-transport-tests PROPERTIES
    C_STANDARD 23 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_options(umicom-fat16-space-transport-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
target_link_options(umicom-fat16-space-transport-tests PRIVATE
    -Wl,--wrap=main
    -Wl,--wrap=UmicomKernelPhysicalMemoryAllocateFrame
    -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
foreach(case IN ITEMS ordinary output_reentry queue_reentry timer_reentry policy_reentry
        command_dispatch operation_dispatch all_read_failures mismatch reset_retry release_retry
        after_close_deadline after_close_rollback arguments)
    add_test(NAME "kernel.fat16_space.transport.${case}" COMMAND umicom-fat16-space-transport-tests
        "${case}" "${CMAKE_CURRENT_SOURCE_DIR}/../disk_inspection/fixture.raw")
    set_tests_properties("kernel.fat16_space.transport.${case}" PROPERTIES TIMEOUT 45)
endforeach()
