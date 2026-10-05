# Umicom Kernel native mapped-region lifetime and provenance tests.
# All C ownership, mapping and physical allocation is real. Only the architecture
# gate/cache fence and deliberate allocation/release failures are modelled.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomMappedRegionsTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_MAPPED_SANITIZERS "Enable native address/undefined sanitizers" OFF)
add_executable(umicom-mapped-region-tests mapped_region_tests.c
    ../../kernel/mapped_regions.c ../../kernel/virtual_memory.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-mapped-region-tests PRIVATE ../../include)
# Only this host project renames the backend entry points for fault injection.
# The production allocator, source comments and freestanding build do not change.
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES COMPILE_DEFINITIONS
    "UmicomKernelPhysicalMemoryAllocateFrame=UmicomMappedActualAllocate;UmicomKernelPhysicalMemoryFreeFrame=UmicomMappedActualFree")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-mapped-region-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_MAPPED_SANITIZERS)
        target_compile_options(umicom-mapped-region-tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-mapped-region-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_MAPPED_CASES
    initialisation-contract planning-is-lazy region-quota page-quota
    null-arguments noncanonical-range crossing-hole arithmetic-overflow
    alignment-refusal zero-page-refusal null-page-refusal permission-refusals
    guard-overlap below-and-above-reservation adjacent-regions high-canonical-region
    seed-and-zero-padding fragmented-backing copy-across-pages copy-range-preflight
    copy-alias-refusal readonly-write-refusal bad-region-handle build-only-once
    borrow-pins-storage bad-lease-return double-return stale-lease
    lease-exhaustion unsafe-operations close-scrubs closed-not-reinitialised
    root-substitution foreign-child-pointer own-table-alias cycle
    invalid-leaf-frame wrong-leaf-permissions mapping-in-guard pte-reserved-bits
    counters-disagree missing-owned-table externally-freed-table externally-freed-data
    duplicate-data-frame metadata-corruption copied-owner
    physical-exhaustion allocation-refusals cleanup-refusals rollback-cleanup-refusal
    combined-map-rollback-refusal interleaved-spaces repeated-lifetimes maximum-layout
    snapshot-accounting source-release-after-build output-unchanged status-names
)
foreach(UMICOM_MAPPED_CASE IN LISTS UMICOM_MAPPED_CASES)
    add_test(NAME kernel.mapped_regions.${UMICOM_MAPPED_CASE}
        COMMAND umicom-mapped-region-tests ${UMICOM_MAPPED_CASE})
endforeach()
