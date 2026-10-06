# Umicom Kernel boot-service policy with real lower owners and modelled machine entry.
# No RISC-V instructions, UART or actual hardware clock is claimed by this test.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomBootServiceTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_BOOT_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-boot-tests boot_services_tests.c
    ../../kernel/boot_services.c ../../kernel/program_launch.c
    ../../kernel/user_streams.c ../../kernel/process_supervisor.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-boot-tests PRIVATE ../../include)
target_compile_definitions(umicom-boot-tests PRIVATE UMICOM_KERNEL_STANDARD_STREAMS=1
    UMICOM_KERNEL_PROGRAM_LAUNCH=1 UMICOM_KERNEL_USER_SLICES=1
    UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_BLOCKING_IPC=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-boot-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    # A test-only wrapper refuses selected releases without changing the real allocator.
    target_link_options(umicom-boot-tests PRIVATE -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
    if(UMICOM_BOOT_SANITIZERS)
        target_compile_options(umicom-boot-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-boot-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_BOOT_CASES normal null-owner zero-count excess-count null-plan
    duplicate-name empty-name bad-name long-name null-image empty-image huge-image
    invalid-launch invalid-environment self-dependency unknown-dependency cycle disconnected-cycle
    zero-attempts excess-attempts zero-timeout excess-timeout excess-retry zero-slices excess-slices
    duplicate-initialise copied-owner unsafe-admission unsafe-step invalid-quantum
    dependency-order multiple-prerequisites optional-failure required-failure transitive-skip
    backoff exact-retry-boundary retry-new-identity snapshot-inputs output-before-collection
    callback-reentry malformed-image deadline budget entry-refusal shutdown-running shutdown-backoff
    clock-reversal deadline-overflow query-invalid cleanup-retry cleanup-outcome-stable repeated-lifetimes program-fault retained-load-retry stop-retained-load
    independent-during-backoff maximum-plan)
foreach(UMICOM_BOOT_CASE IN LISTS UMICOM_BOOT_CASES)
    add_test(NAME kernel.boot_services.${UMICOM_BOOT_CASE}
        COMMAND umicom-boot-tests ${UMICOM_BOOT_CASE})
    set_tests_properties(kernel.boot_services.${UMICOM_BOOT_CASE} PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
endforeach()

# This separate process does not initialise the model allocator before calling
# normal boot, and links no file/process/Framework implementation for recovery.
add_executable(umicom-startup-support-tests startup_support_tests.c
    ../../kernel/boot_memory.c ../../kernel/recovery_console.c ../../kernel/console_line.c
    ../../kernel/address.c ../../kernel/device_tree.c ../../kernel/physical_memory.c)
target_include_directories(umicom-startup-support-tests PRIVATE ../../include)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-startup-support-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_BOOT_SANITIZERS)
        target_compile_options(umicom-startup-support-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-startup-support-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_STARTUP_SUPPORT_CASES memory-valid memory-reservation memory-reinitialise
    memory-hart memory-empty-ram memory-overflow-ram memory-alignment memory-image-below
    memory-image-empty memory-image-outside memory-image-unaligned memory-dtb-below
    memory-dtb-truncated memory-dtb-magic memory-dtb-size memory-dtb-overlap memory-capacity
    recovery-help recovery-status recovery-poweroff recovery-extra-token recovery-prefix
    recovery-case recovery-launch-refused recovery-file-refused recovery-empty recovery-null
    recovery-quote recovery-control recovery-nul)
foreach(UMICOM_STARTUP_SUPPORT_CASE IN LISTS UMICOM_STARTUP_SUPPORT_CASES)
    add_test(NAME kernel.startup_support.${UMICOM_STARTUP_SUPPORT_CASE}
        COMMAND umicom-startup-support-tests ${UMICOM_STARTUP_SUPPORT_CASE})
    set_tests_properties(kernel.startup_support.${UMICOM_STARTUP_SUPPORT_CASE} PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
endforeach()
