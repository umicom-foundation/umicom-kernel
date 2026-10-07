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
    "${UMICOM_ROOT}/kernel/device_tree_firmware.c"
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

# The firmware boundary has its own tests; all strict-reader cases above remain
# intact, including rejection of nonzero property and node-name padding.
add_executable(umicom-firmware-copy-tests firmware_copy_tests.c
    "${UMICOM_ROOT}/kernel/device_tree_firmware.c"
    "${UMICOM_ROOT}/kernel/device_tree_reader.c")
target_include_directories(umicom-firmware-copy-tests PRIVATE "${UMICOM_ROOT}/include")
target_compile_options(umicom-firmware-copy-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fno-omit-frame-pointer)
if(UMICOM_HARDWARE_SANITIZERS)
    target_compile_options(umicom-firmware-copy-tests PRIVATE -fsanitize=address,undefined)
    target_link_options(umicom-firmware-copy-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_FIRMWARE_COPY_CASES
    clean_copy only_property_padding_changes owned_reader_lifetime property_data_preserved
    unaligned_input_and_copy exact_capacity accessible_tail_not_copied every_truncated_prefix
    null_and_pointer_overflow pairwise_ownership_overlap node_padding_remains_strict
    final_validation_and_wipe failure_replaces_prior_reader bad_header_and_sizes structure_bounds
    block_overlap malformed_tokens_and_names malformed_property_extent reservation_validation
    blob_limit_and_maximum future_compatible_version)
foreach(UMICOM_FIRMWARE_COPY_CASE IN LISTS UMICOM_FIRMWARE_COPY_CASES)
    add_test(NAME firmware_copy.${UMICOM_FIRMWARE_COPY_CASE}
        COMMAND umicom-firmware-copy-tests "${UMICOM_FIRMWARE_COPY_CASE}")
    set_tests_properties(firmware_copy.${UMICOM_FIRMWARE_COPY_CASE} PROPERTIES TIMEOUT 30)
endforeach()
