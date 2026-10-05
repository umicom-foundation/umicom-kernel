# Umicom Kernel native interrupt policy. CSR state is explicitly modelled.
# The actual portable controller and QEMU quiescence C adapter are linked.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomInterruptTests LANGUAGES C ASM)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_INTERRUPT_SANITIZERS "Sanitise native policy code" ON)
add_executable(umicom-interrupt-tests interrupt_tests.c model.c
    ../../kernel/interrupts.c ../../platform/qemu-riscv64/interrupt_ownership.c)
target_include_directories(umicom-interrupt-tests PRIVATE ../../include)
target_compile_definitions(umicom-interrupt-tests PRIVATE UMICOM_INTERRUPT_NATIVE_TESTING=1)
target_compile_options(umicom-interrupt-tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
if(UMICOM_INTERRUPT_SANITIZERS)
    target_compile_options(umicom-interrupt-tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(umicom-interrupt-tests PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
set(UMICOM_INTERRUPT_TESTS
    initialise uninitialised reinitialise initialise-enabled initialise-sources
    initialise-hart initialise-vector initialise-scratch initialise-translation initialise-mprv
    null-section zero-owner disabled-restore enabled-restore bit-only-restore nested-restore
    wrong-nested-owner out-of-order stale-section wrong-leave-owner zero-token depth-limit
    poison-mie poison-source poison-vector poison-scratch poison-translation poison-delegation poison-hart
    foreign-vector scope-exhaustion source-needs-section unsupported-source null-lease
    pending-acquire enabled-acquire delegated-acquire future-acquire exclusive-source wrong-source-owner
    wrong-lease enabled-release pending-release future-release source-readback-enable source-readback-disable
    delivery-inside-section source-specific-bits stale-lease switch-before-initialise switch-scope switch-lease switch-restored
    delivery-owner delivery-no-source delivery-boolean delivery-unowned-source delivery-delegated delivery-enable-disable
    unmask-publication repeated-lifetimes modelled-nesting switch-live-delivery switch-live-source)
foreach(UMICOM_INTERRUPT_CASE IN LISTS UMICOM_INTERRUPT_TESTS)
    add_test(NAME kernel.interrupts.${UMICOM_INTERRUPT_CASE} COMMAND umicom-interrupt-tests ${UMICOM_INTERRUPT_CASE})
    set_tests_properties(kernel.interrupts.${UMICOM_INTERRUPT_CASE} PROPERTIES TIMEOUT 20)
endforeach()

# The optional scheduler checks use the retained Linux x86-64 test adapter,
# not another Kernel architecture port. Its alternate stacks do not implement
# ASan fibre notifications, so these four tests use UBSan only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    add_executable(umicom-interrupt-scheduler-tests scheduler_boundary_tests.c model.c
        ../../kernel/interrupts.c ../../platform/qemu-riscv64/interrupt_ownership.c
        ../../kernel/threads.c ../threads/host_adapter.c ../threads/host_context.S)
    target_include_directories(umicom-interrupt-scheduler-tests PRIVATE ../../include)
    target_compile_definitions(umicom-interrupt-scheduler-tests PRIVATE UMICOM_INTERRUPT_NATIVE_TESTING=1)
    target_compile_options(umicom-interrupt-scheduler-tests PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Werror>)
    target_link_options(umicom-interrupt-scheduler-tests PRIVATE
        -Wl,--wrap=UmicomKernelThreadMachineReady -Wl,--wrap=UmicomKernelThreadContextSwitch)
    if(UMICOM_INTERRUPT_SANITIZERS)
        target_compile_options(umicom-interrupt-scheduler-tests PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-fsanitize=undefined;-fno-sanitize-recover=all;-fno-omit-frame-pointer>)
        target_link_options(umicom-interrupt-scheduler-tests PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
    endif()
    foreach(UMICOM_INTERRUPT_BOUNDARY callback-suspension dispatcher-section dispatcher-lease abandoned-completion)
        add_test(NAME kernel.interrupts.${UMICOM_INTERRUPT_BOUNDARY}
            COMMAND umicom-interrupt-scheduler-tests ${UMICOM_INTERRUPT_BOUNDARY})
        set_tests_properties(kernel.interrupts.${UMICOM_INTERRUPT_BOUNDARY} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
