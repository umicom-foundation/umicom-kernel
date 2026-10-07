# Umicom Kernel host-only tests for the real DTB parser, catalogue and boot adapter.
# No QEMU success is inferred from these byte-protocol and ownership checks.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomHardwareCatalogueTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
get_filename_component(UMICOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
option(UMICOM_HARDWARE_SANITIZERS "Use address and undefined-behaviour sanitizers" ON)
add_executable(umicom-hardware-tests device_tree_tests.c
    "${UMICOM_ROOT}/kernel/device_tree_reader.c"
    "${UMICOM_ROOT}/kernel/hardware_catalogue.c"
    "${UMICOM_ROOT}/kernel/hardware_boot.c"
    "${UMICOM_ROOT}/kernel/device_tree.c"
    "${UMICOM_ROOT}/kernel/address.c")
target_include_directories(umicom-hardware-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-hardware-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_HARDWARE_SANITIZERS)
    target_compile_options(umicom-hardware-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-hardware-tests PRIVATE -fsanitize=address,undefined)
endif()
# The names also exist in the C harness. Keep them explicit so configuration
# does not need a source generator, host script or target executable to run.
set(UMICOM_HARDWARE_CASES
    qemu_like_inventory root_only invalid_arguments every_truncated_prefix unaligned_input
    magic total_size blob_limit old_format future_compatible_format incompatible_format
    block_overlap structure_alignment reservation_alignment block_extent reservation_terminator
    reservation_overflow reservation_zero reservation_overlap reservation_capacity unknown_token
    node_underflow missing_root second_root unclosed_root missing_end property_after_child
    node_characters node_padding property_padding name_offset property_extent duplicate_property
    duplicate_child depth_capacity node_capacity property_capacity canonical_paths
    string_suffix_reference input_immutable_output_owned typed_values compatible_list malformed_compatible
    disabled_ancestor empty_ranges_identity absent_ranges_untranslated translated_ranges ambiguous_ranges
    cross_window_register range_overflow range_stride unsupported_cells noninherited_cell_defaults
    register_stride register_overflow zero_phandle duplicate_phandle dangling_interrupt_reference
    malformed_timebase missing_timebase catalogue_capacity multiple_memory_spans register_capacity
    nested_bus_translation deterministic_mutations capture_and_report capture_bounds phandle_alias_mismatch
    chosen_alias_literal failure_outputs node_name_capacity trailing_end reservation_header_overlap zero_sized_resource legacy_zero_phandle range_cell_width_overflow
    register_cell_width_overflow duplicate_hart_id overlapping_memory property_name_capacity
    catalogue_owner_overlap range_capacity)
foreach(UMICOM_HARDWARE_CASE IN LISTS UMICOM_HARDWARE_CASES)
    add_test(NAME hardware.${UMICOM_HARDWARE_CASE}
        COMMAND umicom-hardware-tests "${UMICOM_HARDWARE_CASE}")
    set_tests_properties(hardware.${UMICOM_HARDWARE_CASE} PROPERTIES TIMEOUT 30)
endforeach()
