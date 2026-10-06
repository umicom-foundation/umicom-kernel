# Umicom Kernel console parser, command engine and lifecycle tests.
# The real services are linked. Hardware entry/CSR/timer state is an explicit
# reuse of the earlier native scheduling model; no RISC-V guest pass is implied.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomConsoleShellTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_CONSOLE_SANITIZERS "Enable native address and undefined sanitizers" OFF)
add_executable(umicom-console-tests console_shell_tests.c
    ../../kernel/console_line.c ../../kernel/console_shell.c
    ../../kernel/process_supervisor.c ../../kernel/user_files.c ../../kernel/user_file_service.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-console-tests PRIVATE ../../include)
target_compile_definitions(umicom-console-tests PRIVATE UMICOM_KERNEL_USER_SLICES=1
    UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_BLOCKING_IPC=1 UMICOM_KERNEL_FILE_SERVICES=1)
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES COMPILE_DEFINITIONS
    "UmicomKernelPhysicalMemoryAllocateFrame=UmicomConsoleActualAllocate;UmicomKernelPhysicalMemoryFreeFrame=UmicomConsoleActualFree")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-console-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_CONSOLE_SANITIZERS)
        target_compile_options(umicom-console-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-console-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_CONSOLE_CASES
    line-crlf line-backspace line-controls line-overflow line-limit line-clear-cancel line-invalid-length line-byte-fuzz
    parse-quotes parse-empty-token parse-refusals parse-limits parse-no-expansion parse-numbers parse-byte-fuzz
    initialisation-refusals initialisation-budgets seed-files reinitialisation copied-owner
    basic-file-roundtrip quoted-file-name namespace-refusals unknown-and-arguments overwrite-existing
    binary-display directory-and-types remove-and-reap editor-crlf-once editor-backspace-clear
    editor-nul-and-escape editor-overflow-no-prefix editor-input-loss editor-cancel numeric-range
    malformed-executable load-refusal-rollback run-file-snapshot run-busy-rejects-command
    run-fault-then-retry run-budget run-cancel foreground-typeahead file-client-lifetime
    file-client-fault file-client-cancel read-only-grant explicit-writable-grant file-grant-refusal
    unsafe-command unsafe-return reentrant-output partial-close-retry shutdown-cancels-live
    close-idempotent poweroff repeated-commands repeated-processes memory-and-status)
foreach(UMICOM_CONSOLE_CASE IN LISTS UMICOM_CONSOLE_CASES)
    add_test(NAME kernel.console.${UMICOM_CONSOLE_CASE} COMMAND umicom-console-tests ${UMICOM_CONSOLE_CASE})
endforeach()

# This optional adapter check uses ordinary RAM, never a real host UART. A
# refused fixed mapping skips the check rather than inventing device evidence.
add_executable(umicom-console-uart-tests uart_input_tests.c ../../platform/qemu-riscv64/serial.c)
target_include_directories(umicom-console-uart-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-console-uart-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_CONSOLE_SANITIZERS)
        target_compile_options(umicom-console-uart-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-console-uart-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
foreach(UMICOM_UART_CASE IN ITEMS null-output idle-output-unchanged all-byte-values line-errors error-with-data no-register-writes)
    add_test(NAME kernel.console.uart.${UMICOM_UART_CASE} COMMAND umicom-console-uart-tests ${UMICOM_UART_CASE})
    set_tests_properties(kernel.console.uart.${UMICOM_UART_CASE} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
