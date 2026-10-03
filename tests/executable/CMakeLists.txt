# Umicom Kernel host executable-loader tests.
# These compile real ownership code but do not execute RV64 guest instructions.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomExecutableTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_EXECUTABLE_SANITIZERS "Check native memory accesses with address/undefined sanitizers" OFF)
set(UMICOM_EXECUTABLE_SAMPLE "" CACHE FILEPATH "Optional separately linked RV64 ELF to inspect and load without executing")
add_executable(umicom-executable-tests
    executable_tests.c
    ../../kernel/executable.c
    ../../kernel/process_image.c
    ../../kernel/address.c
    ../../kernel/physical_memory.c
    ../../kernel/virtual_memory.c
    ../../kernel/user_memory.c
)
target_include_directories(umicom-executable-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-executable-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_EXECUTABLE_SANITIZERS)
        target_compile_options(umicom-executable-tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-executable-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_EXECUTABLE_CASES
    valid-inspect null-input null-output unaligned-input-buffer ignored-section-table readable-metadata
    copy-and-zero-fill source-independence two-independent-images page-permissions-and-guards
    owner-lifecycle scrub-released-pages allocation-failure-sweep mutated-input-sweep
    truncated-header too-large bad-magic wrong-class wrong-endian ident-format wrong-machine
    shared-object header-format header-size foreign-os-abi foreign-abi-tag float-abi unknown-abi-flags
    empty-header-table oversized-header-table header-entry-size header-overlap header-offset-wrap truncated-table
    dynamic-segment interpreter-segment tls-segment unknown-segment executable-stack metadata-outside-file
    file-larger-than-memory segment-offset-wrap segment-truncated empty-load null-page upper-canonical
    virtual-range-wrap canonical-hole cross-canonical-limit alignment-not-power-two file-virtual-misaligned
    writable-code write-only execute-only-profile unknown-permissions overlap-pages disjoint-bytes-shared-page
    unsorted-loads stack-collision stack-guard-collision page-budget entry-in-bss entry-read-only entry-odd
    entry-without-rvc no-loads too-many-loads
)
foreach(UMICOM_EXECUTABLE_CASE IN LISTS UMICOM_EXECUTABLE_CASES)
    add_test(NAME kernel.executable.${UMICOM_EXECUTABLE_CASE}
        COMMAND umicom-executable-tests ${UMICOM_EXECUTABLE_CASE})
endforeach()
if(UMICOM_EXECUTABLE_SAMPLE)
    if(NOT EXISTS "${UMICOM_EXECUTABLE_SAMPLE}")
        message(FATAL_ERROR "UMICOM_EXECUTABLE_SAMPLE does not name an existing ELF file")
    endif()
    add_test(NAME kernel.executable.real-linked-elf
        COMMAND umicom-executable-tests real-linked-elf "${UMICOM_EXECUTABLE_SAMPLE}")
endif()
