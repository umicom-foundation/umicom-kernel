# Umicom Kernel native object-cache and physical-frame observation tests.
# Real C allocation/guard/bitmap logic is used. The architecture gate is a model,
# and a renamed allocator symbol allows a deliberate, non-destructive refusal.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomObjectCacheTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_OBJECT_SANITIZERS "Enable native address/undefined sanitizers" OFF)
add_executable(umicom-object-cache-tests object_cache_tests.c
    ../../kernel/object_cache.c ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-object-cache-tests PRIVATE ../../include)
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES COMPILE_DEFINITIONS
    "UmicomKernelPhysicalMemoryFreeFrame=UmicomObjectActualFrameFree")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-object-cache-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_OBJECT_SANITIZERS)
        target_compile_options(umicom-object-cache-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-object-cache-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_OBJECT_CASES
    null-init
    size-zero
    size-too-large
    alignment-zero
    alignment-non-power
    alignment-too-large
    quota-zero
    quota-too-large
    dirty-storage
    repeated-init
    copied-cache
    unsafe-init
    backend-uninitialised
    lazy-init
    geometry-grid
    small-objects-share-frame
    page-alignment
    noncontiguous-frames
    initial-zeroing
    free-scrubs-padding
    close-live-refused
    trim-live-preserved
    trim-empty
    reuse-cached-slot
    trim-reuse-stale-ticket
    wrong-cache
    forged-ticket
    interior-pointer
    foreign-pointer
    null-reference
    zero-ticket
    double-free
    cache-capacity
    physical-exhaustion
    exhaustion-recovery
    ticket-exhaustion
    guard-poison-retains
    externally-freed-frame
    reserved-backing-frame
    duplicate-frame
    counter-corruption
    unused-slot-corruption
    out-of-range-frame
    invalid-state
    trim-partial-retry
    close-partial-retry
    release-refusal-retains
    outputs-unchanged
    repeated-lifetimes
    allocation-budgets
    interleaved-lifetimes
    frame-query-uninitialised
    frame-query-null
    frame-query-alignment
    frame-query-range
    frame-query-classes
    frame-query-nonmutating
    resolve-live
    unsafe-operations
    closed-not-reinitialised
    status-names
    null-outputs
    trim-retry-every-position
    object-data-not-metadata
)
foreach(UMICOM_OBJECT_CASE IN LISTS UMICOM_OBJECT_CASES)
    add_test(NAME kernel.object_cache.${UMICOM_OBJECT_CASE}
        COMMAND umicom-object-cache-tests ${UMICOM_OBJECT_CASE})
endforeach()

# Run the guest's C orchestration as an additional labelled host-model test.
# It catches acceptance-sequence mistakes without claiming privileged execution.
add_executable(umicom-object-cache-guest-model guest_model.c
    ../../kernel/object_cache.c ../../kernel/object_cache_validation.c
    ../../kernel/physical_memory.c ../../kernel/address.c)
target_include_directories(umicom-object-cache-guest-model PRIVATE ../../include)
# The physical allocator's source-local renamed release symbol is shared by
# this project, so this host model calls it through a target-local declaration.
target_compile_definitions(umicom-object-cache-guest-model PRIVATE
    UmicomKernelPhysicalMemoryFreeFrame=UmicomObjectActualFrameFree)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-object-cache-guest-model PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_OBJECT_SANITIZERS)
        target_compile_options(umicom-object-cache-guest-model PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-object-cache-guest-model PRIVATE -fsanitize=address,undefined)
    endif()
endif()
add_test(NAME kernel.object_cache.guest-sequence-model COMMAND umicom-object-cache-guest-model)
