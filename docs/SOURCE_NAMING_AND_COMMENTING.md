# Umicom Kernel Source Naming and Educational Commenting Standard

## Purpose

Kernel source should remain understandable years after a feature was introduced. Git history records chronology; source code records architecture, behaviour, ownership, invariants and failure rules.

## Naming

New Kernel identifiers use the full `Umicom` name:
- independent `umicom-kernel` repository;
- architecture and decision register;
- originality/reuse policy;
- native ABI/portability direction;
- promotion gates;
- main-only repository workflow.

## RISC-V freestanding boot — COMPLETE
```text
UmicomKernel...
UmicomPlatform...
UmicomRiscv...
UMICOM_KERNEL_...
UMICOM_RISCV_...
```
Qualified on QEMU RISC-V `virt`:

Short experimental spellings may remain only in clearly isolated source-compatibility aliases while existing code is migrated. New implementation logic must not introduce additional short names.
- Assembly `_start`;
- bootstrap stack;
- BSS clearing;
- C23 `UmiKernelMain`;
- polling NS16550 serial output;
- QEMU firmware hand-off with `-bios`;
- test-finisher clean exit;
- real CTest QEMU boot.

## No batch or temporary build identity in implementation source

Do not put temporary development batch numbers, milestone numbers, build IDs or release chronology into:

- function or method names;
- type names;
- constants/macros used by runtime code;
- C/Assembly comments;
- runtime readiness markers;
- CTest capability names;
- implementation-document filenames.

Use descriptive capability names instead. For example:

```text
UMICOM_KERNEL_PHYSICAL_MEMORY_READY
kernel.riscv64.virtual_memory
UmicomKernelVirtualMemoryTranslate
```

## Educational comments

Comments should explain useful engineering information such as:

- why an instruction or register operation is required;
- register/CSR semantics;
- ownership and lifetime;
- memory layout and alignment;
- why an overflow or bounds check exists;
- what a failure result means;
- why a platform-specific operation is isolated behind an adapter;
- what is deliberately not implemented yet and why.

Avoid comments that only restate syntax without teaching intent.

## Preservation

When behaviour is superseded, preserve important implementation history according to the project source-preservation rule. Git history remains the authoritative chronology, so removing temporary batch terminology from the active source does not erase the engineering record.
