# Umicom Kernel event tests use the existing real host alternate-stack adapter.
# This optional Linux target tests C coordination, not RV64 privilege behaviour.
# ASan fibre notifications are not implemented in that adapter; use UBSan here.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomEventTests LANGUAGES C ASM)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "This optional native suite requires Linux x86-64. Use the main RV64 QEMU tests on Windows.")
endif()
option(UMICOM_EVENT_UBSAN "Use undefined-behaviour checks in native C" ON)
add_executable(umicom-event-tests events_tests.c ../threads/host_adapter.c ../threads/host_context.S
    ../../kernel/threads.c ../../kernel/events.c)
target_include_directories(umicom-event-tests PRIVATE ../../include)
target_compile_options(umicom-event-tests PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(UMICOM_EVENT_UBSAN)
    target_compile_options(umicom-event-tests PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-fsanitize=undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>)
    target_link_options(umicom-event-tests PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
endif()
set(UMICOM_EVENT_CASES
    null-initialise uninitialised machine-admission reinitialise copied-domain
    invalid-mode invalid-boolean null-output capacity invalid-token
    stale-token retirement manual-latch auto-latch auto-coalescing
    auto-fifo manual-broadcast reset-awarded close-awarded reset-latch
    wait-any-index wait-any-order preflight-whole-set duplicate-set empty-set
    oversized-set outside-callback spurious-wake deadline late-signal
    deadline-maximum immediate-timeout ready-at-admission signal-before-deadline spurious-timed-wake
    close-wait-any close-reuse-before-resume cancel-wait cancel-reap cancel-awarded
    ticket-exhaustion corrupt-event corrupt-wait scheduler-poison separate-domains
    repeated-lifetimes all-waiter-slots
)
foreach(UMICOM_EVENT_CASE IN LISTS UMICOM_EVENT_CASES)
    add_test(NAME kernel.events.${UMICOM_EVENT_CASE} COMMAND umicom-event-tests ${UMICOM_EVENT_CASE})
    set_tests_properties(kernel.events.${UMICOM_EVENT_CASE} PROPERTIES TIMEOUT 20)
endforeach()

# Run the same C acceptance sequence used by the guest. Hardware observations
# are explicitly modelled; thread stacks and event/message implementations are real.
add_executable(umicom-event-acceptance-model guest_model.c ../threads/host_adapter.c ../threads/host_context.S
    ../../kernel/threads.c ../../kernel/events.c ../../kernel/event_validation.c ../../kernel/message_channel.c)
target_include_directories(umicom-event-acceptance-model PRIVATE ../../include)
target_compile_options(umicom-event-acceptance-model PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
if(UMICOM_EVENT_UBSAN)
    target_compile_options(umicom-event-acceptance-model PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-fsanitize=undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>)
    target_link_options(umicom-event-acceptance-model PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
endif()
add_test(NAME kernel.events.acceptance-model COMMAND umicom-event-acceptance-model)
set_tests_properties(kernel.events.acceptance-model PROPERTIES TIMEOUT 20)
