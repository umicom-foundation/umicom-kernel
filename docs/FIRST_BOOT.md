# First Boot — First Original Umicom Kernel Boot

Status: educational source delivery baseline  
Target: QEMU RISC-V 64 `virt`  
Implementation: C23 + minimum required RV64 Assembly  
Framework dependency: none  
Python dependency: none  
PowerShell-script dependency: none

## Purpose

the first-boot foundation proves one small but real native-kernel vertical slice:

1. Clang emits RV64 machine code from original Umicom C23 and Assembly.
2. LLD creates a freestanding RISC-V ELF with `_start` at `0x80200000`.
3. QEMU starts its RISC-V `virt` machine.
4. The the first-boot foundation ELF is loaded as the machine-mode firmware using `-bios`.
5. QEMU's reset ROM transfers control to the ELF firmware entry.
6. The first Umicom instruction executed is `_start` in `arch/riscv64/boot.S`.
7. The boot hart is selected.
8. Machine interrupts remain disabled because the first-boot foundation has no trap handler yet.
9. The bootstrap stack is established.
10. BSS is explicitly cleared.
11. Execution enters `UmiKernelMain()` in freestanding C23.
12. The QEMU NS16550 UART adapter writes deterministic serial evidence.
13. The QEMU SiFive test-finisher terminates the emulator as a successful smoke test.

the first-boot foundation deliberately does **not** implement:

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

## Why the first-boot foundation is loaded with `-bios`

This detail matters because RISC-V QEMU has a reset stage before the image at
`0x80200000`.

the first-boot foundation is machine-mode code.  Its Assembly startup reads machine CSRs such as:

```text
mhartid
mie
```

so it must begin in machine mode.

For this milestone the correct launch is:

```powershell
qemu-system-riscv64.exe `
    -machine virt `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none `
    -monitor none `
    -serial stdio `
    -m 128M `
    -smp 1 `
    -no-reboot
```

Do **not** use:

```text
-bios none -kernel umicom-kernel.elf
```

for the first-boot foundation.

Why:

1. `-kernel` loads our ELF into RAM at its linked low address (`0x80200000`).
2. With `-bios none`, there is no firmware image whose entry replaces the
   firmware start address.
3. QEMU's generated RISC-V reset ROM therefore still jumps toward the firmware
   start at `0x80000000`.
4. Our `_start` is at `0x80200000`, so it is never reached.
5. No UART output is produced and QEMU appears to hang.

Loading our ELF as `-bios <file>` solves the problem without adding OpenSBI or
another firmware dependency to the first-boot foundation.  QEMU's firmware loader accepts an ELF,
takes its ELF entry as the firmware start address, and the reset ROM jumps
there in machine mode.

Later Umicom Kernel milestones may introduce a more formal boot protocol, but
the first-boot foundation intentionally keeps the first hand-off small and observable.

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

## RAM layout

With:

```text
-m 128M
```

QEMU `virt` RAM spans:

```text
0x80000000 .. 0x87ffffff
```

the first-boot foundation deliberately begins at:

```text
0x80200000
```

The first 2 MiB are outside this linker's kernel-owned range.

Therefore the linker describes:

```text
ORIGIN = 0x80200000
LENGTH = 126M
```

rather than incorrectly claiming another full 128 MiB above `0x80200000`.

## Expected serial evidence

```text
UMICOM_KERNEL_BEGIN
name=Umicom Kernel
version=0.1.0
milestone=the first-boot foundation
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

before configuring the first-boot foundation.

## Validation boundary

A successful build proves that Clang and LLD produced the requested RV64 ELF.

A successful QEMU run additionally proves that:

- QEMU loaded the the first-boot foundation ELF as machine-mode firmware;
- its reset ROM transferred control to `_start`;
- our Assembly bootstrap executed;
- the bootstrap could call C23 code;
- MMIO serial output worked;
- the guest reached the QEMU success finisher.

A QEMU pass still does not prove physical hardware support or a complete
operating system.

## Next milestone

the trap/timer foundation preserves the complete the first-boot foundation boot path and adds the RISC-V trap/exception/timer
foundation with educational C23/Assembly commentary.
