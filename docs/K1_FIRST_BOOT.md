# K1 — First Original Umicom Kernel Boot

Status: educational source delivery baseline  
Target: QEMU RISC-V 64 `virt`  
Implementation: C23 + minimum required RV64 Assembly  
Framework dependency: none  
Python dependency: none  
PowerShell-script dependency: none

## Purpose

K1 proves one small but real native-kernel vertical slice:

1. Clang emits RV64 machine code from original Umicom C23 and Assembly.
2. LLD creates a freestanding RISC-V ELF at the address selected by our linker script.
3. QEMU starts a RISC-V `virt` machine with `-bios none`.
4. The first instruction executed from our image is in `arch/riscv64/boot.S`.
5. The boot hart is selected.
6. Interrupts remain disabled because K1 has no trap handler yet.
7. The bootstrap stack is established.
8. BSS is explicitly cleared.
9. Execution enters `UmiKernelMain()` in freestanding C23.
10. The QEMU NS16550 UART adapter writes deterministic serial evidence.
11. The QEMU SiFive test-finisher terminates the emulator as a successful smoke test.

K1 deliberately does **not** implement:

- dynamic memory allocation;
- page tables/virtual memory;
- trap handling;
- timer interrupts;
- scheduling;
- user mode;
- filesystems;
- networking;
- normal device discovery;
- Framework integration;
- POSIX compatibility.

Those are later milestones.

## Ownership boundaries

```text
arch/riscv64/
    ISA/bootstrap and linker concerns

platform/qemu-riscv64/
    QEMU virt machine MMIO concerns

kernel/
    architecture-neutral freestanding kernel logic

include/umicom/kernel/
    first native-kernel contracts
```

Generic kernel code does not contain the QEMU UART or test-finisher addresses.

## Expected serial evidence

```text
UMICOM_KERNEL_BEGIN
name=Umicom Kernel
version=0.1.0
milestone=K1
arch=riscv64
machine=qemu-virt
build=k1-riscv64-freestanding
hart=0
dtb=0x................
state=booted
UMICOM_KERNEL_END
```

The exact DTB address is selected by QEMU and is not a fixed contract.

## CMake preset comments

JSON does not allow ordinary comments. Read:

```text
docs/CMAKE_PRESET_REFERENCE.md
```

for a field-by-field explanation of `CMakePresets.json`.

## Windows tool setup

Read:

```text
docs/WINDOWS_BEGINNER_SETUP.md
```

before configuring K1. It includes installation and PATH verification for every
host tool used by this milestone.

## Validation boundary

A successful build proves that Clang and LLD produced the requested RV64 ELF.

A successful QEMU run additionally proves that:

- the selected QEMU machine loaded the image;
- our Assembly bootstrap received control;
- the bootstrap could call C23 code;
- MMIO serial output worked;
- the guest reached the success finisher.

A QEMU pass still does not prove physical hardware support or a complete
operating system.

## Next milestone

K2 preserves the complete K1 boot path and adds the RISC-V trap/exception/timer
foundation with educational C23/Assembly commentary.
