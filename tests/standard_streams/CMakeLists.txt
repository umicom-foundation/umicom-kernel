# Umicom Kernel standard streams on the actual C scheduler and checked copies.
# Native instruction entry is modelled; no host UART or RISC-V execution is
# implied. Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomStandardStreamTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_STREAM_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-stream-tests standard_streams_tests.c
    ../../kernel/user_streams.c ../../kernel/console_terminal.c
    ../../kernel/console_line.c ../../kernel/console_shell.c
    ../../kernel/process_supervisor.c ../../kernel/user_files.c ../../kernel/user_file_service.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-stream-tests PRIVATE ../../include)
target_compile_definitions(umicom-stream-tests PRIVATE UMICOM_KERNEL_STANDARD_STREAMS=1
    UMICOM_KERNEL_TERMINAL=1 UMICOM_KERNEL_USER_SLICES=1 UMICOM_KERNEL_MESSAGE_CHANNELS=1
    UMICOM_KERNEL_BLOCKING_IPC=1 UMICOM_KERNEL_FILE_SERVICES=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-stream-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_STREAM_SANITIZERS)
        target_compile_options(umicom-stream-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-stream-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_STREAM_CASES
    attach-lifetime attach-null attach-twice attach-dirty attach-late copied-owner
    grant-invalid grant-twice grant-late no-grant exact-session wrong-direction flags
    zero-transfer excessive-transfer invalid-input-buffer invalid-output-buffer
    cross-page-input cross-page-output invalid-second-input-page invalid-second-output-page
    nonblocking-empty nonblocking-full input-copy-ownership output-copy-ownership
    input-ring-wrap input-capacity output-order output-tail-zero
    wait-input no-user-spin wait-eof buffered-eof wait-revalidate
    wait-output output-snapshot repeated-pump remaining-output-after-close
    cancel-input cancel-output stale-replacement explicit-discard close-live
    last-slice-wait cumulative-call-limit context-refusal refused-entry unsafe-return
    corrupt-count corrupt-packet task-isolation repeated-lifetimes mixed-stream-operations
    terminal-attach terminal-line terminal-crlf terminal-eof terminal-partial-eof
    terminal-edit terminal-rejected-line terminal-overlong terminal-input-loss terminal-input-full
    terminal-control-output terminal-cancel terminal-shutdown terminal-restart)
foreach(UMICOM_STREAM_CASE IN LISTS UMICOM_STREAM_CASES)
    add_test(NAME kernel.standard_streams.${UMICOM_STREAM_CASE}
        COMMAND umicom-stream-tests ${UMICOM_STREAM_CASE})
endforeach()
