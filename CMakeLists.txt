#-----------------------------------------------------------------------------
# Umicom Kernel
# File: CMakeLists.txt
#
# PURPOSE:
#   Build K1, the first original freestanding Umicom Kernel image for QEMU's
#   RISC-V "virt" machine.
#
# EDUCATIONAL NOTE:
#   CMake is used only as the build description.  The kernel itself is still
#   C23 and RISC-V Assembly.  No Python, PowerShell or shell script is needed
#   by the K1 kernel build.
#
# AUTHOR AND ORGANISATION:
#   Sammy Hegab
#   Umicom Foundation
#
# LICENCE:
#   MIT
#-----------------------------------------------------------------------------

# Require a CMake version new enough to understand the preset/toolchain
# behaviour used by this repository.
cmake_minimum_required(VERSION 3.24)

# Name the project and tell CMake that this milestone contains C and Assembly.
# "ASM" enables the RISC-V .S bootstrap file as a first-class source file.
project(UmicomKernel LANGUAGES C ASM)

# Request the C23 language standard for every C source in this small project.
set(CMAKE_C_STANDARD 23)

# Refuse silently falling back to an older C standard if a compiler cannot
# satisfy the C23 request.
set(CMAKE_C_STANDARD_REQUIRED ON)

# Disable compiler-specific C language extensions so the C sources remain
# intentionally portable and standards-oriented.
set(CMAKE_C_EXTENSIONS OFF)

# Load CMake's testing support so the real QEMU boot can be registered as a
# CTest case when the emulator is available.
include(CTest)

# K1 is intentionally one architecture only.  Failing here gives a beginner a
# clear message instead of producing an incorrectly linked host executable.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    message(FATAL_ERROR
        "K1 currently supports only the riscv64 cross-build preset. "
        "Configure with: cmake --preset riscv64-clang-debug")
endif()

# Store the linker-script path once.  The linker script controls the physical
# addresses and section layout of the freestanding kernel image.
set(
    UMICOM_KERNEL_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/linker.ld"
)

# Create the first kernel ELF executable from:
#   - one architecture bootstrap Assembly source;
#   - generic kernel C23 code;
#   - the QEMU RISC-V machine adapter used only for K1.
add_executable(
    umicom-kernel
    arch/riscv64/boot.S
    kernel/console.c
    kernel/main.c
    platform/qemu-riscv64/finish.c
    platform/qemu-riscv64/serial.c
)

# Make the repository's public/internal kernel headers visible while compiling
# this target.  No hosted operating-system SDK is added here.
target_include_directories(
    umicom-kernel
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Configure the code generator for the first RISC-V target.
#
# -march=rv64imac_zicsr:
#   RV64 base integer ISA plus multiply/divide, atomics, compressed
#   instructions and CSR instructions needed by early privileged code.
#
# -mabi=lp64:
#   64-bit pointers and long integers, with the standard integer calling
#   convention and no floating-point register ABI.
#
# -mcmodel=medany:
#   Generate position-addressing sequences suitable for code/data within the
#   RISC-V medium-anywhere model used by this simple linked image.
#
# -msmall-data-limit=0:
#   Do not create a small-data area that would require a global-pointer setup
#   before C code can safely execute.
#
# -ffreestanding:
#   Tell the compiler this is not a normal hosted C program with an OS/libc.
#
# -fno-builtin:
#   Do not silently replace our code with calls/assumptions about libc helpers.
#
# -fno-stack-protector:
#   A hosted stack protector needs runtime support K1 does not have yet.
#
# -fno-pic / -fno-pie:
#   K1 is linked to a deliberate fixed physical address.
#
# -fno-unwind-tables / -fno-asynchronous-unwind-tables:
#   K1 has no unwinder and should not carry unused hosted-runtime metadata.
#
# The warning flags intentionally make questionable beginner code visible.
target_compile_options(
    umicom-kernel
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

# Configure the final kernel link.
#
# -nostdlib:
#   Do not link the host C runtime, startup objects or libc.
#
# -fuse-ld=lld:
#   Use LLVM's LLD linker because the Clang cross-compiler is driving the link.
#
# -Wl,-T,<file>:
#   Pass our memory/section layout to the ELF linker.
#
# -Wl,-Map,...:
#   Produce a human-readable map file that beginners can inspect.
#
# --gc-sections:
#   Discard unreachable sections instead of carrying accidental unused code.
#
# --build-id=none:
#   Avoid an automatically generated note section in this tiny milestone.
#
# --no-relax:
#   Keep instruction/address sequences stable while the early boot code and
#   memory layout are still being taught and inspected.
target_link_options(
    umicom-kernel
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

# Give the output a kernel-specific .elf filename and place it in a predictable
# bin directory inside the selected build tree.
set_target_properties(
    umicom-kernel
    PROPERTIES
        OUTPUT_NAME "umicom-kernel"
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"

        # Re-link if the memory-layout file changes even when C/Assembly sources
        # are otherwise unchanged.
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}"
)

# Locate QEMU automatically.
#
# Beginners often install QEMU through MSYS2, which puts it in
# C:/msys64/ucrt64/bin.  The second hint covers a common standalone Windows
# installation.  Normal PATH discovery still works as well.
find_program(
    UMICOM_QEMU_RISCV64
    NAMES
        qemu-system-riscv64
        qemu-system-riscv64.exe
    HINTS
        "C:/msys64/ucrt64/bin"
        "C:/Program Files/qemu"
)

# Register a real emulator boot test only when:
#   1. testing is enabled; and
#   2. the RISC-V QEMU executable was actually found.
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)

    # CTest launches the same kernel ELF a developer would launch manually.
    #
    # K1 is deliberately machine-mode code: boot.S reads machine CSRs such as
    # mhartid and mie before any supervisor-mode runtime exists.
    #
    # Therefore load this ELF as QEMU's firmware image with "-bios <file>".
    # QEMU's RISC-V firmware loader understands ELF, records the ELF entry as
    # the firmware start address, and its small reset ROM then jumps to that
    # address while providing a0=hart ID and a1=device-tree address.
    #
    # Do NOT use "-bios none -kernel <ELF>" for K1.  In current QEMU "virt"
    # direct-kernel handling, "-kernel" loads our ELF at 0x80200000, but with
    # no firmware the reset ROM's firmware start address remains 0x80000000.
    # The processor then jumps into empty RAM before our _start instruction,
    # producing the completely silent hang that this repair documents.
    add_test(
        NAME kernel.k1.riscv64.qemu_boot
        COMMAND "${UMICOM_QEMU_RISCV64}"
            -machine virt
            -bios "$<TARGET_FILE:umicom-kernel>"
            -display none
            -monitor none
            -serial stdio
            -m 128M
            -smp 1
            -no-reboot
    )

    # Keep a hung early kernel from blocking the test suite forever.
    #
    # PASS_REGULAR_EXPRESSION proves the serial path reached the final K1 marker.
    #
    # FAIL_REGULAR_EXPRESSION prevents obvious failure text from being accepted
    # merely because some earlier success marker happened to be printed.
    set_tests_properties(
        kernel.k1.riscv64.qemu_boot
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_END"
            FAIL_REGULAR_EXPRESSION "panic;PANIC;failed;FAILED"
    )
else()

    # This is intentionally informational rather than a fake passing test:
    # building an ELF is not the same evidence as actually booting it.
    message(
        STATUS
        "qemu-system-riscv64 not found: K1 will build, but the real QEMU CTest "
        "is not registered. Install QEMU and re-run the CMake configure step."
    )
endif()
