# Umicom Kernel native user-scheduling policy tests. Hardware entry is modelled;
# every C owner, mapper, copy validator and dispatcher below is the real source.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomUserSchedulingTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_USER_SCHEDULING_SANITIZERS "Use native address/undefined sanitizers" OFF)
add_executable(umicom-user-scheduling-tests
    user_scheduling_tests.c
    ../../kernel/user_scheduler.c
    ../../kernel/user_slice.c
    ../../arch/riscv64/user_slice.c
    ../../kernel/user_monitor.c
    ../../kernel/executable.c
    ../../kernel/process_image.c
    ../../kernel/address.c
    ../../kernel/physical_memory.c
    ../../kernel/virtual_memory.c
    ../../kernel/user_memory.c)
target_include_directories(umicom-user-scheduling-tests PRIVATE ../../include)
target_compile_definitions(umicom-user-scheduling-tests PRIVATE UMICOM_KERNEL_USER_SLICES=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-user-scheduling-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_USER_SCHEDULING_SANITIZERS)
        target_compile_options(umicom-user-scheduling-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-user-scheduling-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_USER_SCHEDULING_CASES
    null-initialise reinitialise copied-owner malformed-admission invalid-admission-budget
    capacity identity-exhaustion idle invalid-quantum round-robin all-integer-state
    interrupted-pc syscall-then-pause address-space-separation paused-reap-refused lower-destructor-refused
    cancel-unstarted cancel-paused stale-handle-reuse forged-token generation-exhaustion
    quantum-budget fault-and-survivor exit-is-terminal cumulative-call-budget
    ownership-refusal timer-in-use clock-overflow wrong-hart pmp-policy live-mprv live-fpu hardware-refusal
    privileged-frame odd-pc unmapped-pc reserved-word floating-frame wrong-xlen zero-user-stack
    early-timer timer-storm wrong-session-binding recursive-binding corrupt-control-state
    corrupt-counter-policy compare-not-restored wrong-trap-privilege invalid-timer-pc reentry
    empty-retained-reap allocation-rollback repeated-lifetimes)
foreach(UMICOM_USER_SCHEDULING_CASE IN LISTS UMICOM_USER_SCHEDULING_CASES)
    add_test(NAME kernel.user_scheduling.${UMICOM_USER_SCHEDULING_CASE}
        COMMAND umicom-user-scheduling-tests ${UMICOM_USER_SCHEDULING_CASE})
endforeach()
