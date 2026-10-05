# Umicom Kernel process-supervision native checks. The actual C owners are
# linked; only RISC-V entry, timer/CSR operations and an explicit allocator
# failure boundary are models. This optional project is not part of the guest.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomProcessSupervisionTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_SUPERVISION_SANITIZERS "Use native address and undefined sanitizers" OFF)
add_executable(umicom-supervision-tests supervision_tests.c
    ../../kernel/process_supervisor.c
    ../../kernel/user_ipc.c ../../kernel/user_scheduler.c ../../kernel/user_slice.c
    ../../arch/riscv64/user_slice.c ../../kernel/user_monitor.c
    ../../kernel/message_channel.c ../../kernel/message_service.c
    ../../kernel/executable.c ../../kernel/process_image.c ../../kernel/address.c
    ../../kernel/physical_memory.c ../../kernel/virtual_memory.c ../../kernel/user_memory.c)
target_include_directories(umicom-supervision-tests PRIVATE ../../include)
target_compile_definitions(umicom-supervision-tests PRIVATE UMICOM_KERNEL_BLOCKING_IPC=1
    UMICOM_KERNEL_MESSAGE_CHANNELS=1 UMICOM_KERNEL_USER_SLICES=1)
# Failure injection wraps the real allocator boundary. Normal calls still use
# its real bitmaps and accounting; neither the loader nor the destructor is mocked.
set_source_files_properties(../../kernel/physical_memory.c PROPERTIES COMPILE_DEFINITIONS
    "UmicomKernelPhysicalMemoryFreeFrame=UmicomSupervisionActualFrameFree")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-supervision-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_SUPERVISION_SANITIZERS)
        target_compile_options(umicom-supervision-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-supervision-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_SUPERVISION_CASES
    null-init repeated-init copied-owner invalid-spawn unknown-parent capacity
    child-authority sibling-refusal self-refusal ancestor-refusal terminal-parent
    argument-before-run argument-after-run normal-completion collect-live collect-once
    collect-any-empty collect-any-waiting collect-any-owned completion-retained
    adopt-on-exit adopt-on-fault cancel-descendants grandchild-policy stop-tree
    single-cancel duplicate-cancel cancel-paused cancel-blocked unrelated-peer
    message-exchange queued-message-survives collect-ipc-cleanup bad-connect connect-after-run
    entry-refusal reentrant-call task-budget syscall-budget invalid-context
    machine-state-failure bad-output stale-generation repeated-lifetimes
    cleanup-retry partial-cleanup-retry retained-load rollback-budgets shutdown
    shutdown-idempotent shutdown-rejects-spawn poison-no-free corrupt-parent corrupt-record)
foreach(UMICOM_SUPERVISION_CASE IN LISTS UMICOM_SUPERVISION_CASES)
    add_test(NAME kernel.supervision.${UMICOM_SUPERVISION_CASE}
        COMMAND umicom-supervision-tests ${UMICOM_SUPERVISION_CASE})
endforeach()
