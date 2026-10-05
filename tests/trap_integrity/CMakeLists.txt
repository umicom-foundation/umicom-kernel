# Umicom Kernel native trap-frame policy checks. This executable tests actual
# C policy, not trap entry Assembly, interrupt delivery or hardware privilege.
# Sammy Hegab, Umicom Foundation. MIT licence.
cmake_minimum_required(VERSION 3.24)
project(UmicomTrapIntegrityTests LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
include(CTest)
option(UMICOM_TRAP_SANITIZERS "Use address and undefined-behaviour sanitizers" OFF)
add_executable(umicom-trap-integrity-tests trap_integrity_tests.c
    ../../kernel/trap_integrity.c ../../arch/riscv64/trap.c)
target_include_directories(umicom-trap-integrity-tests PRIVATE ../../include)
target_compile_options(umicom-trap-integrity-tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
if(UMICOM_TRAP_SANITIZERS)
    target_compile_options(umicom-trap-integrity-tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(umicom-trap-integrity-tests PRIVATE -fsanitize=address,undefined)
endif()
set(UMICOM_TRAP_CASES ecall timer load-probe store-probe null-before null-after
    empty-text invalid-route privilege-0 privilege-1 privilege-2 live-mie
    floating-state vector-state ordinary-mprv probe-without-mprv unaligned-pc
    pc-below-text pc-after-text pc-at-last-instruction pc-overflow user-ecall-cause
    supervisor-ecall-cause unknown-cause timer-missing-interrupt changed-cause
    changed-trap-value changed-reserved repeated-ecall wrong-instruction-length
    return-enables-mie return-changes-mpp return-enables-mprv timer-mpie-not-cleared
    probe-mprv-not-cleared zero-interrupted-stack unaligned-interrupted-stack
    original-ecall-policy original-timer-policy)
foreach(UMICOM_TRAP_REG RANGE 1 31)
    list(APPEND UMICOM_TRAP_CASES register-${UMICOM_TRAP_REG})
endforeach()
foreach(UMICOM_TRAP_CASE IN LISTS UMICOM_TRAP_CASES)
    add_test(NAME kernel.trap_policy.${UMICOM_TRAP_CASE}
        COMMAND umicom-trap-integrity-tests ${UMICOM_TRAP_CASE})
endforeach()
