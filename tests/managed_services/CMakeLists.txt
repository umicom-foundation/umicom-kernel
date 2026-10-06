# Umicom Kernel managed-service policy with real lower owners and modelled machine entry.
# No RISC-V instructions, UART or actual hardware clock is claimed by this test.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomManagedServiceTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_MANAGED_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-managed-tests service_manager_tests.c
    ../../kernel/service_manager.c ../../kernel/service_manager_validation.c ../../kernel/service_console.c ../../kernel/console_line.c ../../kernel/program_launch.c
    ../../kernel/user_streams.c ../../kernel/process_supervisor.c
    ../../kernel/user_scheduler.c ../../kernel/user_slice.c ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c ../../kernel/user_ipc.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-managed-tests PRIVATE ../../include)
target_compile_definitions(umicom-managed-tests PRIVATE UMICOM_KERNEL_MANAGED_SERVICES=1 UMICOM_KERNEL_STANDARD_STREAMS=1
    UMICOM_KERNEL_PROGRAM_LAUNCH=1 UMICOM_KERNEL_USER_SLICES=1
    UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_BLOCKING_IPC=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-managed-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    # A test-only wrapper refuses selected releases without changing the real allocator.
    target_link_options(umicom-managed-tests PRIVATE -Wl,--wrap=UmicomKernelPhysicalMemoryFreeFrame)
    if(UMICOM_MANAGED_SANITIZERS)
        target_compile_options(umicom-managed-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-managed-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_MANAGED_CASES ready-and-heartbeat park-no-spin continuation-once dependent-waits-for-ready restart-revokes-consumers new-identity readiness-deadline health-deadline health-renewal exit-zero-not-ready fault-retry optional-failure transitive-loss cleanup-retry close-retry shutdown-no-restart slice-budget call-budget bad-sequence bad-operation reserved-register copy-session duplicate-ready late-report snapshot-plan callback-reentry accepted-output entry-refusal unsafe-context invalid-quantum reinitialise copied-owner clock-reversal overflow-deadline budgeted-restart out-of-order-plan maximum-plan malformed-image stale-session-unbound corrupt-return repeated-lifetimes null-owner null-plan zero-count excess-count empty-name bad-name duplicate-name empty-image null-image large-image invalid-launch self-dependency unknown-dependency cycle disconnected-cycle zero-attempts excess-attempts zero-startup excess-startup zero-health excess-health zero-interval interval-equals-health excess-retry zero-slices excess-slices invalid-required guest-controller-sequence console-not-started console-invalid-mode console-live-reports console-restart console-stop-retains-report console-domain-selection console-cleanup-retry console-restart-input-refusal)
foreach(UMICOM_MANAGED_CASE IN LISTS UMICOM_MANAGED_CASES)
    add_test(NAME kernel.managed_services.${UMICOM_MANAGED_CASE}
        COMMAND umicom-managed-tests ${UMICOM_MANAGED_CASE})
    set_tests_properties(kernel.managed_services.${UMICOM_MANAGED_CASE} PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1;ASAN_OPTIONS=detect_leaks=1:halt_on_error=1")
endforeach()
