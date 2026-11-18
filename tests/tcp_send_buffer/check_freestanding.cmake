# Umicom Kernel
# File: tests/tcp_send_buffer/check_freestanding.cmake
#
# PURPOSE:
#   Link RV64 send storage and sequence services using LLVM without host libraries.
#
# AUTHOR AND ORGANISATION:
# Sammy Hegab
# Umicom Foundation
#
# LICENCE:
# MIT
foreach(required CLANG LEVEL SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
# Select the target-capable linker explicitly; a Windows GNU linker cannot
# link RV64 objects even when Clang compiled them for the correct target.
execute_process(COMMAND "${CLANG}" -fuse-ld=lld --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64
    -std=c23 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib "-O${LEVEL}"
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    "-I${SOURCE}/include" "${SOURCE}/kernel/tcp_sequence.c" "${SOURCE}/kernel/tcp_transmit.c"
    "${SOURCE}/kernel/tcp_send_buffer.c" "${SOURCE}/tests/tcp_send_buffer/freestanding_entry.c"
    -Wl,-e,_start -o "${WORK}/send.elf"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 25)
file(WRITE "${WORK}/link.log" "${out}\n${err}\nexit=${result}\n")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "RV64 link failed: ${result}\n${out}\n${err}")
endif()
file(READ "${WORK}/send.elf" magic OFFSET 0 LIMIT 4 HEX)
if(NOT magic STREQUAL "7f454c46")
    message(FATAL_ERROR "Expected ELF output")
endif()
