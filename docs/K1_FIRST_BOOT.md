# K1 — First Original Umicom Kernel Boot

Status: source delivery baseline  
Target: QEMU RISC-V 64 `virt`  
Implementation: C23 + minimum RV64 Assembly  
Framework dependency: none

## Purpose

K1 proves only the first native-kernel vertical slice:

1. QEMU starts an RV64 machine without OpenSBI or Linux.
2. `arch/riscv64/boot.S` receives control.
3. The boot hart is selected, interrupts remain disabled, the bootstrap stack is established and BSS is cleared.
4. Execution enters `UmiKernelMain` in freestanding C23.
5. The machine-specific NS16550 UART adapter writes deterministic boot evidence.
6. The QEMU SiFive test-finisher ends the emulator with a success result.

K1 deliberately does **not** implement memory allocation, traps, scheduling, user mode, filesystems, devices beyond the bootstrap UART/test finisher, Framework integration or POSIX compatibility.

## Ownership boundaries

```text
arch/riscv64/
    ISA/bootstrap concerns

platform/qemu-riscv64/
    QEMU virt machine MMIO

kernel/
    architecture-neutral kernel logic

include/umicom/kernel/
    first native-kernel contracts
```

Generic kernel code does not contain QEMU UART/test-device addresses.

## Expected serial evidence

A successful run prints fields similar to:

```text
UMICOM_KERNEL_BEGIN
name=Umicom Kernel
version=0.1.0
milestone=K1
arch=riscv64
machine=qemu-virt
build=k1-riscv64-freestanding
hart=0
dtb=0x00000000........
state=booted
UMICOM_KERNEL_END
```

The exact DTB address is supplied by QEMU and is not a fixed K1 contract.

## Validation boundary

A successful build proves that Clang/LLD produced the requested RV64 ELF.

A successful QEMU test proves that the named QEMU `virt` machine transferred control to the kernel, the bootstrap entered C23, serial output worked and the guest reached the test finisher.

It does not prove:

- physical RISC-V hardware support;
- interrupt handling;
- a scheduler;
- user mode;
- persistent storage;
- networking;
- an Umicom Framework runtime;
- a complete operating system.

Those remain future milestones.

## Next milestone

K2 introduces the architecture/HAL and trap foundation: machine trap vector, exception report, CLINT/ACLINT timer discovery strategy and deterministic timer evidence while preserving the K1 boot path.
