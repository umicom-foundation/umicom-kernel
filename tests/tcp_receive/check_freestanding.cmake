# Umicom Kernel
# File: tests/tcp_receive/check_freestanding.cmake
# PURPOSE: Refuse missing tools and link the receive storage and sequence helpers without any host library.
# AUTHOR AND ORGANISATION: Sammy Hegab / Umicom Foundation
# LICENCE: MIT
foreach(required CLANG LEVEL SOURCE WORK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
# Clang can otherwise choose the host-only GNU linker. Keep that earlier
# probe command for review; the active command requests LLVM target support.
if(FALSE)
execute_process(COMMAND "${CLANG}" --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64
    -std=c23 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib "-O${LEVEL}"
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    "-I${SOURCE}/include" "${SOURCE}/kernel/tcp_sequence.c" "${SOURCE}/kernel/tcp_receive.c"
    "${SOURCE}/tests/tcp_receive/freestanding_entry.c"
    -Wl,-e,_start -o "${WORK}/receive.elf"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 25)
endif()
execute_process(COMMAND "${CLANG}" -fuse-ld=lld --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64
    -std=c23 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib "-O${LEVEL}"
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    "-I${SOURCE}/include" "${SOURCE}/kernel/tcp_sequence.c" "${SOURCE}/kernel/tcp_receive.c"
    "${SOURCE}/tests/tcp_receive/freestanding_entry.c"
    -Wl,-e,_start -o "${WORK}/receive.elf"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 25)
file(WRITE "${WORK}/link.log" "${out}\n${err}\nexit=${result}\n")
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "RV64 link failed: ${result}\n${out}\n${err}")
endif()
file(READ "${WORK}/receive.elf" magic OFFSET 0 LIMIT 4 HEX)
if(NOT magic STREQUAL "7f454c46")
    message(FATAL_ERROR "Expected ELF output")
endif()
