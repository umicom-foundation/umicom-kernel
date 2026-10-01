#-----------------------------------------------------------------------------
# Umicom Kernel
# File: CMakeLists.txt
#
# PURPOSE:
#   Build K1, the first original freestanding Umicom Kernel image for QEMU's
#   RISC-V virt machine, using C23 plus the minimum architecture Assembly.
#
# AUTHOR AND ORGANISATION:
#   Sammy Hegab
#   Umicom Foundation
#
# LICENCE:
#   MIT
#-----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.24)
project(UmicomKernel LANGUAGES C ASM)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    message(FATAL_ERROR "K1 currently supports only the riscv64 cross-build preset")
endif()

set(UMICOM_KERNEL_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/linker.ld")

add_executable(umicom-kernel
    arch/riscv64/boot.S
    kernel/console.c
    kernel/main.c
    platform/qemu-riscv64/finish.c
    platform/qemu-riscv64/serial.c
)

target_include_directories(umicom-kernel
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_options(umicom-kernel
    PRIVATE
        -march=rv64imac_zicsr
        -mabi=lp64
        -mcmodel=medany
        -msmall-data-limit=0
        -ffreestanding
        -fno-builtin
        -fno-stack-protector
        -fno-pic
        -fno-pie
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Werror
)

target_link_options(umicom-kernel
    PRIVATE
        -march=rv64imac_zicsr
        -mabi=lp64
        -mcmodel=medany
        -nostdlib
        -fuse-ld=lld
        "-Wl,-T,${UMICOM_KERNEL_LINKER_SCRIPT}"
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/umicom-kernel.map"
        -Wl,--gc-sections
        -Wl,--build-id=none
        -Wl,--no-relax
)

set_target_properties(umicom-kernel PROPERTIES
    OUTPUT_NAME "umicom-kernel"
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}"
)

find_program(UMICOM_QEMU_RISCV64 qemu-system-riscv64)
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)
    add_test(
        NAME kernel.k1.riscv64.qemu_boot
        COMMAND "${UMICOM_QEMU_RISCV64}"
            -machine virt
            -bios none
            -display none
            -monitor none
            -serial stdio
            -m 128M
            -smp 1
            -kernel "$<TARGET_FILE:umicom-kernel>"
    )
    set_tests_properties(kernel.k1.riscv64.qemu_boot PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_END"
        FAIL_REGULAR_EXPRESSION "panic;PANIC;failed;FAILED"
    )
else()
    message(STATUS
        "qemu-system-riscv64 not found: K1 will build, but the QEMU CTest is not registered")
endif()
