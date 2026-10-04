# Umicom Kernel cooperative scheduler tests with real host alternate stacks.
# This test-only System V adapter does not claim an x86 Kernel port.
# AddressSanitizer is deliberately not enabled here: these hand-written stack
# switches do not implement its fibre notification protocol. UBSan is supported.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomThreadTests LANGUAGES C ASM)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "The optional host context suite requires Linux x86-64. Use the normal RV64 QEMU tests on Windows.")
endif()
option(UMICOM_THREAD_UBSAN "Enable undefined-behaviour sanitizer for native C" ON)
add_executable(umicom-thread-tests threads_tests.c host_adapter.c host_context.S
    ../../kernel/threads.c ../../kernel/message_channel.c)
target_include_directories(umicom-thread-tests PRIVATE ../../include)
target_compile_options(umicom-thread-tests PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(UMICOM_THREAD_UBSAN)
    target_compile_options(umicom-thread-tests PRIVATE $<$<COMPILE_LANGUAGE:C>:-fsanitize=undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>)
    target_link_options(umicom-thread-tests PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
endif()
set(UMICOM_THREAD_CASES
    null-initialise reinitialise copied-domain dirty-domain uninitialised
    null-entry null-output admission-machine full-capacity output-unchanged
    round-robin nested-locals current-query nested-dispatch peer-domain-dispatch
    yield-outside wait-outside sleep-outside past-deadline sleep-deadline
    equal-deadlines next-deadline backwards-clock maximum-clock
    wake-waiter wake-sleeper wake-ready wake-terminal
    cancel-ready cancel-sleeping cancel-waiting cancel-terminal cancel-current
    reap-live reap-current terminal-value scrub-stack stale-handle generation-reuse
    generation-retirement separate-domains stack-low stack-high saved-stack
    bad-current bad-next corrupt-state dispatch-overflow yield-overflow
    empty-idle suspended-idle repeated-lifetimes bounded-dispatch message-exchange)
foreach(UMICOM_THREAD_CASE IN LISTS UMICOM_THREAD_CASES)
    add_test(NAME kernel.threads.${UMICOM_THREAD_CASE} COMMAND umicom-thread-tests ${UMICOM_THREAD_CASE})
    set_tests_properties(kernel.threads.${UMICOM_THREAD_CASE} PROPERTIES TIMEOUT 10)
endforeach()

# Execute the guest's C validation sequence with explicitly modelled hardware.
# This catches orchestration errors without pretending to boot a RISC-V CPU.
add_executable(umicom-thread-acceptance-model guest_model.c host_adapter.c host_context.S host_probe.S
    ../../kernel/threads.c ../../kernel/thread_validation.c ../../kernel/message_channel.c)
target_include_directories(umicom-thread-acceptance-model PRIVATE ../../include)
target_compile_options(umicom-thread-acceptance-model PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(UMICOM_THREAD_UBSAN)
    target_compile_options(umicom-thread-acceptance-model PRIVATE $<$<COMPILE_LANGUAGE:C>:-fsanitize=undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>)
    target_link_options(umicom-thread-acceptance-model PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
endif()
add_test(NAME kernel.threads.acceptance-model COMMAND umicom-thread-acceptance-model)
set_tests_properties(kernel.threads.acceptance-model PROPERTIES TIMEOUT 10)
