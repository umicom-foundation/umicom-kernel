# Umicom Kernel native message-channel and syscall validation.
# These tests use the real queues, ownership checks, page mapper and C trap
# dispatcher. They do not emulate RISC-V privilege changes or run guest code.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomMessageTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_MESSAGE_SANITIZERS "Enable address/undefined-behaviour sanitizers" OFF)
add_executable(umicom-message-tests
    message_tests.c
    ../../kernel/message_channel.c
    ../../kernel/message_service.c
    ../../kernel/user_monitor.c
    ../../kernel/user_memory.c
    ../../kernel/address.c
    ../../kernel/physical_memory.c
    ../../kernel/virtual_memory.c
)
target_include_directories(umicom-message-tests PRIVATE ../../include)
target_compile_definitions(umicom-message-tests PRIVATE UMICOM_KERNEL_MESSAGE_CHANNELS=1)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(umicom-message-tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(UMICOM_MESSAGE_SANITIZERS)
        target_compile_options(umicom-message-tests PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(umicom-message-tests PRIVATE -fsanitize=address,undefined)
    endif()
endif()
set(UMICOM_MESSAGE_CASES
    null-domain uninitialised dirty-storage reinitialise copied-domain
    invalid-owner unknown-rights aliased-output internal-output
    send-receive source-copy duplex empty-queue full-queue zero-payload
    oversized-payload maximum-payload internal-source short-destination
    peek-preserves consume-sequence peer-close-drain receiver-close-discard
    reference-survives duplicate-subset duplicate-right grant-owner grant-right
    grant-no-escalation irreversible-restrict zero-rights-close stale-handle
    generation-reuse generation-retirement sequence-exhaustion
    owner-quota same-owner-pair-quota handle-capacity channel-capacity
    close-owner-shared separate-domains corrupt-reference-count
    corrupt-message-length corrupt-sequence corrupt-boolean corrupt-empty-channel corrupt-padding scrub-consumed scrub-recycled
    repeated-lifetimes
    binding-unbound binding-nested binding-wrong-identity binding-wrong-unbind
    syscall-send syscall-receive syscall-owner-refusal syscall-register-return
    syscall-invalid-pc syscall-machine-trap syscall-call-budget
    syscall-cross-page-send syscall-cross-page-receive syscall-send-gap
    syscall-receive-gap syscall-readonly syscall-overflow syscall-too-small
    syscall-empty syscall-query syscall-close syscall-unknown
)
foreach(UMICOM_MESSAGE_CASE IN LISTS UMICOM_MESSAGE_CASES)
    add_test(NAME kernel.message_channels.${UMICOM_MESSAGE_CASE}
        COMMAND umicom-message-tests ${UMICOM_MESSAGE_CASE})
    set_tests_properties(kernel.message_channels.${UMICOM_MESSAGE_CASE} PROPERTIES TIMEOUT 30)
endforeach()
