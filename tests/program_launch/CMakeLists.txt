# Umicom Kernel structured launch tests on the actual scheduler and checked copies.
# Native instruction entry is modelled; no host UART or RISC-V execution is
# implied. Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomProgramLaunchTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_LAUNCH_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-launch-tests program_launch_tests.c
    ../../kernel/program_launch.c ../../programs/launch_client/main.c
    ../../kernel/user_streams.c ../../kernel/console_terminal.c
    ../../kernel/console_line.c ../../kernel/console_shell.c
    ../../kernel/process_supervisor.c ../../kernel/user_files.c ../../kernel/user_file_service.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/vfs.c ../../kernel/ramfs.c ../../kernel/object_cache.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-launch-tests PRIVATE ../../include)
target_compile_definitions(umicom-launch-tests PRIVATE UMICOM_KERNEL_STANDARD_STREAMS=1
    UMICOM_KERNEL_PROGRAM_LAUNCH=1 UMICOM_KERNEL_TERMINAL=1 UMICOM_KERNEL_USER_SLICES=1 UMICOM_KERNEL_MESSAGE_CHANNELS=1
    UMICOM_KERNEL_BLOCKING_IPC=1 UMICOM_KERNEL_FILE_SERVICES=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-launch-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_LAUNCH_SANITIZERS)
        target_compile_options(umicom-launch-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-launch-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_LAUNCH_CASES
    normal-layout null-spec null-output no-arguments null-arguments empty-program-name null-string empty-null-string embedded-nul long-string max-string too-many-arguments maximum-arguments null-environment no-environment too-many-environment maximum-environment duplicate-environment case-sensitive-environment prefix-environment bad-environment-name numeric-environment-name empty-environment-name missing-equals empty-environment-value text-capacity text-overflow misaligned-base zero-base overflow-base noncanonical-base canonical-boundary upper-canonical task-normal task-stale-task task-invalid-plan-atomic task-unsafe-context task-active-refused task-wrong-backing task-input-snapshot task-numeric-after-structured task-double-prepare task-paused-prepare repeated-lifetimes tokenizer-maximum tokenizer-overflow tokenizer-quotes tokenizer-late-quote parent-authority wrong-parent shell-readonly shell-writable shell-invalid program-actual-c program-numeric program-bad-cookie program-missing-sentinel)
foreach(UMICOM_LAUNCH_CASE IN LISTS UMICOM_LAUNCH_CASES)
    add_test(NAME kernel.program_launch.${UMICOM_LAUNCH_CASE}
        COMMAND umicom-launch-tests ${UMICOM_LAUNCH_CASE})
    # Sanitizer findings must fail, not merely print diagnostics with exit zero.
    set_tests_properties(kernel.program_launch.${UMICOM_LAUNCH_CASE} PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
endforeach()
