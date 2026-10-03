# Umicom Kernel native process-registry validation.
# Use the actual loader/allocator/mapper; only the RV64 execution boundary is
# substituted in the test translation unit. No host result claims a guest boot.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomProcessRegistryTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_REGISTRY_SANITIZERS "Use address and undefined-behaviour sanitizers" OFF)
set(UMICOM_REGISTRY_SAMPLE "" CACHE FILEPATH "Optional real RV64 ELF for native loading only")
add_executable(umicom-process-registry-tests
    process_registry_tests.c
    ../../kernel/process_registry.c
    ../../kernel/process_image.c
    ../../kernel/executable.c
    ../../kernel/address.c
    ../../kernel/physical_memory.c
    ../../kernel/virtual_memory.c
    ../../kernel/user_memory.c
)
target_include_directories(umicom-process-registry-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-process-registry-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_REGISTRY_SANITIZERS)
        target_compile_options(umicom-process-registry-tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-process-registry-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
# Each named test starts with fresh process-local BSS. Sweeps retain generations
# throughout their iterations so accidental token resurrection remains visible.
set(UMICOM_REGISTRY_CASES
    null-registry uninitialised copied-registry reinitialise dirty-initial-storage
    bad-owner bad-rights null-output empty-image malformed-image create-query
    zero-handle bad-index bad-generation wrong-owner stale-after-close slot-reuse
    final-close scrub-on-release query-only duplicate-subset duplicate-requires-right
    grant-requires-right no-escalation restrict-irreversible zero-rights-close
    grant-owner-binding owner-cleanup-shared duplicate-no-allocation unknown-rights
    grant-requires-transfer grant-requires-duplicate grant-no-escalation
    object-capacity identity-exhaustion generation-retirement owner-capacity handle-capacity
    run-route run-refused unsafe-run-return recursive-run-guard
    cleanup-retry owner-cleanup-partial retained-reap corrupt-reference-count
    allocation-failure-sweep repeated-lifetimes
)
foreach(UMICOM_REGISTRY_CASE IN LISTS UMICOM_REGISTRY_CASES)
    add_test(NAME kernel.process_registry.${UMICOM_REGISTRY_CASE}
        COMMAND umicom-process-registry-tests ${UMICOM_REGISTRY_CASE})
    set_tests_properties(kernel.process_registry.${UMICOM_REGISTRY_CASE} PROPERTIES TIMEOUT 30)
endforeach()
if(UMICOM_REGISTRY_SAMPLE)
    if(NOT EXISTS "${UMICOM_REGISTRY_SAMPLE}")
        message(FATAL_ERROR "UMICOM_REGISTRY_SAMPLE does not name an existing ELF")
    endif()
    add_test(NAME kernel.process_registry.real-linked-elf
        COMMAND umicom-process-registry-tests real-linked-elf "${UMICOM_REGISTRY_SAMPLE}")
endif()
