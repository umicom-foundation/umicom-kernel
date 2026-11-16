# Umicom Kernel
# File: tests/tcp_sequence/check_freestanding.cmake
# PURPOSE: Refuse missing tools and link the production helper without any host library.
# AUTHOR AND ORGANISATION: Sammy Hegab / Umicom Foundation
# LICENCE: MIT
foreach(required CLANG LEVEL SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${CLANG}" --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64
    -std=c23 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib "-O${LEVEL}"
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    "-I${SOURCE}/include" "${SOURCE}/kernel/tcp_sequence.c"
    "${SOURCE}/tests/tcp_sequence/freestanding_entry.c"
    -Wl,-e,_start -o "${WORK}/sequence.elf"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 25)
file(WRITE "${WORK}/link.log" "${out}\n${err}\nexit=${result}\n")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "RV64 link failed: ${result}\n${out}\n${err}")
endif()
file(READ "${WORK}/sequence.elf" magic OFFSET 0 LIMIT 4 HEX)
if(NOT magic STREQUAL "7f454c46")
    message(FATAL_ERROR "Expected ELF output")
endif()
