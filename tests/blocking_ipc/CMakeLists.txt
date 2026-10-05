# Umicom Kernel blocking IPC native tests. Hardware entry and time are models;
# every linked C service below is the real implementation. No cross-build needs
# this optional host suite. Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomBlockingIpcTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_BLOCKING_IPC_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-blocking-ipc-tests blocking_ipc_tests.c
    ../../kernel/user_ipc.c ../../kernel/user_scheduler.c ../../kernel/user_slice.c
    ../../arch/riscv64/user_slice.c ../../kernel/user_monitor.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-blocking-ipc-tests PRIVATE ../../include)
target_compile_definitions(umicom-blocking-ipc-tests PRIVATE UMICOM_KERNEL_BLOCKING_IPC=1
    UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_USER_SLICES=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-blocking-ipc-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_BLOCKING_IPC_SANITIZERS)
        target_compile_options(umicom-blocking-ipc-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-blocking-ipc-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_BLOCKING_IPC_CASES
    null-and-repeat-attach copied-domain startup-argument unbound-call wrong-owner
    nonblocking-preserved zero-timeout bad-buffer read-only-buffer small-capacity
    oversized-capacity oversized-timeout rights empty-send deadline-overflow ready-at-admission
    send-snapshot cancel-send send-peer-close send-timeout hardware-refusal occupied-binding
    call-budget slice-budget order-exhaustion idle-with-waiters fifo-waiters cancel-oldest
    repeated-lifetimes resume-once no-query-right maximum-copy timeout deadline-wins before-deadline
    spurious-pump cancel-and-reuse peer-cancel peer-fault peer-exit peer-budget token-closed right-revoked
    destination-unmapped destination-readonly small-delivery clock-reversal corrupt-frame corrupt-generation)
foreach(UMICOM_BLOCKING_IPC_CASE IN LISTS UMICOM_BLOCKING_IPC_CASES)
    add_test(NAME kernel.blocking_ipc.${UMICOM_BLOCKING_IPC_CASE}
        COMMAND umicom-blocking-ipc-tests ${UMICOM_BLOCKING_IPC_CASE})
endforeach()
