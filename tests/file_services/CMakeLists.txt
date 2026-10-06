# Umicom Kernel checked file services on the real C scheduler and filesystem.
# Privileged entry reuses the established host model. Backend allocation/unpin
# refusal is explicit test injection. Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomFileServiceTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_FILE_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-file-service-tests file_services_tests.c
    ../../kernel/user_files.c ../../kernel/user_file_service.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-file-service-tests PRIVATE ../../include)
target_compile_definitions(umicom-file-service-tests PRIVATE UMICOM_KERNEL_FILE_SERVICES=1
    UMICOM_KERNEL_USER_SLICES=1 UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_BLOCKING_IPC=1)
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES
    COMPILE_DEFINITIONS "UmicomKernelPhysicalMemoryAllocateFrame=UmicomFileActualAllocate")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-file-service-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_FILE_SANITIZERS)
        target_compile_options(umicom-file-service-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-file-service-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_FILE_CASES
    attach-lifetime repeat-attach copied-owner invalid-rights duplicate-grant late-grant late-attach
    no-grant wrong-session read-only-client path-snapshot request-snapshot write-snapshot input-cross-page
    invalid-request-address invalid-result-address malformed-wire-size result-recheck
    missing-input-tail read-invalid-tail read-result-overlap huge-transfer embedded-nul reserved-fields
    unknown-operation null-zero-io descriptor-locality independent-position duplicate-position duplicate-rights
    stat-wire directory-wire directory-change resize-zero-fill append partial-write close-stale-token
    terminal-exit terminal-fault cancellation paused-file-lifetime slice-budget last-slice-open
    close-before-reap cleanup-retry reuse-no-inheritance repeated-lifetimes ipc-blocked-cancel
    context-refusal wrong-hart-refusal unverified-machine-retention
    ipc-final-slice grant-allocation-refusal grant-client-limit)
foreach(UMICOM_FILE_CASE IN LISTS UMICOM_FILE_CASES)
    add_test(NAME kernel.file_services.${UMICOM_FILE_CASE} COMMAND umicom-file-service-tests ${UMICOM_FILE_CASE})
endforeach()
