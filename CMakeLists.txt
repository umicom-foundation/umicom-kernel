#-----------------------------------------------------------------------------
# Umicom Kernel
# File: CMakeLists.txt
#
# PURPOSE:
#   Build the original freestanding Umicom Kernel for QEMU's RISC-V "virt"
#   machine while preserving previously established execution capabilities.
#
# EDUCATIONAL NOTE:
#   CMake describes the host-side build and test graph only.  The kernel image
#   itself is C23 plus the minimum RISC-V Assembly required for bootstrap and
#   trap entry/return.  No Python, PowerShell or shell script is required by
#   the kernel build.
#
# AUTHOR AND ORGANISATION:
#   Sammy Hegab
#   Umicom Foundation
#
# LICENCE:
#   MIT
#-----------------------------------------------------------------------------

# Require the first CMake release whose preset/toolchain behaviour this
# repository deliberately targets.
cmake_minimum_required(VERSION 3.24)

# Tell CMake that the source contains C and preprocessed Assembly (.S).
project(UmicomKernel LANGUAGES C ASM)

# Compile ordinary C source using the C23 language standard.
set(CMAKE_C_STANDARD 23)

# Fail configuration rather than silently compiling as an older C standard.
set(CMAKE_C_STANDARD_REQUIRED ON)

# Disable compiler-specific C dialect extensions in ordinary C source.
set(CMAKE_C_EXTENSIONS OFF)

# Enable CTest so real QEMU boots can be registered as executable tests.
include(CTest)

# The current configuration intentionally supports one execution architecture.  A clear configuration
# failure is safer than accidentally building a host executable.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    message(FATAL_ERROR
        "The current Kernel configuration supports only the riscv64 cross-build preset. "
        "Configure with: cmake --preset riscv64-clang-debug")
endif()

# Keep the memory-layout file in one named variable because both the linker and
# CMake dependency tracking need the same source path.
set(
    UMICOM_KERNEL_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/arch/riscv64/linker.ld"
)

# Build one freestanding ELF image.
#
# The source grouping is intentional:
#   boot.S
#       Creates the initial C environment.
#
#   mmu.S
#       Owns privileged satp, PMP, MPRV and sfence.vma operations that cannot be
#       expressed as ordinary portable C23.
#
#   trap.S
#       Owns the machine trap entry/return sequence because register save/restore
#       and MRET cannot be expressed safely as ordinary portable C.
#
#   trap.c
#       Preserves trap cause policy/state as a regression foundation.
#
#   kernel/address.c + kernel/device_tree.c + kernel/physical_memory.c
#       Checked-address, DTB-range inspection and physical-frame ownership.
#
#   kernel/console.c + kernel/main.c
#       Architecture-neutral teaching/output/orchestration logic.
#
#   platform/qemu-riscv64/*.c
#       QEMU "virt" MMIO and fixed RAM-profile details that generic Kernel
#       allocator code must not know.
add_executable(
    umicom-kernel
    arch/riscv64/boot.S
    arch/riscv64/mmu.S
    arch/riscv64/trap.S
    arch/riscv64/trap.c
    kernel/address.c
    kernel/console.c
    kernel/device_tree.c
    kernel/main.c
    kernel/physical_memory.c
    kernel/virtual_memory.c
    platform/qemu-riscv64/finish.c
    platform/qemu-riscv64/memory.c
    platform/qemu-riscv64/serial.c
    platform/qemu-riscv64/timer.c
)

# Expose only this repository's kernel headers to the target.
target_include_directories(
    umicom-kernel
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Configure code generation for the first RISC-V target.
#
# -march=rv64imac_zicsr
#   RV64 integer ISA plus multiply/divide, atomics, compressed instructions and
#   explicit CSR instructions used by machine-mode trap code.
#
# -mabi=lp64
#   Select the integer-only RV64 C calling convention.
#
# -mcmodel=medany
#   Permit code/data references appropriate for this linked kernel image.
#
# -msmall-data-limit=0
#   Prevent implicit dependence on a global-pointer setup before C executes.
#
# -ffreestanding
#   Tell Clang there is no hosted operating system or C runtime beneath us.
#
# -fno-builtin
#   Prevent compiler substitutions that could introduce libc assumptions.
#
# -fno-stack-protector
#   Hosted stack-protector runtime support does not exist yet.
#
# -fno-pic / -fno-pie
#   The Kernel deliberately links at a fixed physical address.
#
# -fno-unwind-tables / -fno-asynchronous-unwind-tables
#   The Kernel has no unwinder and does not need hosted unwind metadata.
#
# Warning flags remain strict because low-level mistakes should be visible.
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

# Link without any hosted startup/runtime library.
#
# --no-relax is especially useful in early teaching milestones because it keeps
# instruction/address transformations easier to compare with the written source.
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

# Give the image a predictable name/location and re-link whenever the linker
# script changes.
set_target_properties(
    umicom-kernel
    PROPERTIES
        OUTPUT_NAME "umicom-kernel"
        SUFFIX ".elf"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
        LINK_DEPENDS "${UMICOM_KERNEL_LINKER_SCRIPT}"
)

# Discover the RISC-V QEMU system emulator.
#
# PATH remains the normal discovery mechanism.  The explicit hints cover the
# MSYS2 UCRT64 installation recommended by the beginner guide and a common
# standalone QEMU installation.
find_program(
    UMICOM_QEMU_RISCV64
    NAMES
        qemu-system-riscv64
        qemu-system-riscv64.exe
    HINTS
        "C:/msys64/ucrt64/bin"
        "C:/Program Files/qemu"
)

# Register real emulator tests only when QEMU actually exists.
if(BUILD_TESTING AND UMICOM_QEMU_RISCV64)

    # Store the common machine arguments once so every capability test boots
    # exactly the same virtual hardware.
    #
    # aclint=off is explicit even though it is QEMU's current default.  The
    # timer adapter intentionally uses the SiFive-compatible CLINT memory map;
    # naming the mode here prevents an emulator option change from silently
    # changing the device layout beneath the teaching code.
    set(
        UMICOM_QEMU_RISCV64_ARGUMENTS
        -machine "virt,aclint=off"
        -bios "$<TARGET_FILE:umicom-kernel>"
        -display none
        -monitor none
        -serial stdio
        -m 128M
        -smp 1
        -no-reboot
    )

    # Preserve the original bootstrap path as a regression test.
    #
    # Every later capability still has to prove that the firmware hand-off,
    # Assembly bootstrap, C23 entry and serial path work.
    add_test(
        NAME kernel.riscv64.boot
        COMMAND
            "${UMICOM_QEMU_RISCV64}"
            ${UMICOM_QEMU_RISCV64_ARGUMENTS}
    )

    # Require the boot-readiness marker and fail on explicit Kernel/test errors.
    set_tests_properties(
        kernel.riscv64.boot
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_BOOT_READY"
            FAIL_REGULAR_EXPRESSION
                "UMICOM_KERNEL_FAIL;panic;PANIC;failed;FAILED"
    )

    # Preserve synchronous-exception and asynchronous-timer handling as a
    # regression gate for later memory and process work.
    add_test(
        NAME kernel.riscv64.trap_timer
        COMMAND
            "${UMICOM_QEMU_RISCV64}"
            ${UMICOM_QEMU_RISCV64_ARGUMENTS}
    )

    # This marker can be reached only after the controlled ECALL exception and
    # machine-timer interrupt both return successfully.
    set_tests_properties(
        kernel.riscv64.trap_timer
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_TRAP_TIMER_READY"
            FAIL_REGULAR_EXPRESSION
                "UMICOM_KERNEL_FAIL;unexpected-trap;panic;PANIC;failed;FAILED"
    )

    # Preserve physical-frame ownership, protection and accounting as a
    # cumulative real-QEMU regression test.
    add_test(
        NAME kernel.riscv64.physical_memory
        COMMAND
            "${UMICOM_QEMU_RISCV64}"
            ${UMICOM_QEMU_RISCV64_ARGUMENTS}
    )

    # Physical-memory readiness requires RAM/Kernel/DTB reservations, refusal
    # paths and final invariant/accounting checks to complete successfully.
    set_tests_properties(
        kernel.riscv64.physical_memory
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_PHYSICAL_MEMORY_READY"
            FAIL_REGULAR_EXPRESSION
                "UMICOM_KERNEL_FAIL;invariant-failure;panic;PANIC;failed;FAILED"
    )

    # Validate software-managed Sv39 page-table construction, mapping,
    # translation, unmapping and page-table frame reclamation.
    add_test(
        NAME kernel.riscv64.virtual_memory
        COMMAND
            "${UMICOM_QEMU_RISCV64}"
            ${UMICOM_QEMU_RISCV64_ARGUMENTS}
    )

    # The virtual-memory marker is emitted only after the hierarchy returns all
    # temporary page-table/data frames to the physical-memory baseline.
    set_tests_properties(
        kernel.riscv64.virtual_memory
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_VIRTUAL_MEMORY_READY"
            FAIL_REGULAR_EXPRESSION
                "UMICOM_KERNEL_FAIL;corrupt-page-table;invariant-failure;panic;PANIC;failed;FAILED"
    )


    # Prove that the processor's real Sv39 translation machinery interprets
    # the page tables produced by the Kernel the same way as the software page
    # walker.  The validation remains in machine mode and uses MPRV for two
    # controlled supervisor-effective data accesses, so instruction fetch and
    # the active Kernel stack are not yet placed behind translated addresses.
    add_test(
        NAME kernel.riscv64.hardware_translation
        COMMAND
            "${UMICOM_QEMU_RISCV64}"
            ${UMICOM_QEMU_RISCV64_ARGUMENTS}
    )

    # The readiness marker is printed only after a translated load returns the
    # physical seed value, a translated store changes the underlying physical
    # frame, address translation is disabled again, and all temporary page-table
    # and data-frame ownership returns to the physical-memory baseline.
    set_tests_properties(
        kernel.riscv64.hardware_translation
        PROPERTIES
            TIMEOUT 10
            PASS_REGULAR_EXPRESSION "UMICOM_KERNEL_HARDWARE_TRANSLATION_READY"
            FAIL_REGULAR_EXPRESSION
                "UMICOM_KERNEL_FAIL;unexpected-trap;corrupt-page-table;invariant-failure;panic;PANIC;failed;FAILED"
    )
else()

    # Do not invent a passing emulator result when QEMU is absent.
    message(
        STATUS
        "qemu-system-riscv64 not found: the Kernel will build, but real QEMU CTests "
        "are not registered. Install QEMU and re-run the configure step."
    )
endif()
