# the physical-memory foundation delivery manifest — Physical Memory Foundation

Baseline repository:

```text
umicom-foundation/umicom-kernel
5cb799edbbee796d9aa93a9f09c73803f8b11237
feat(kernel): add RISC-V trap and timer foundation
```

## New source files

```text
include/umicom/kernel/address.h
include/umicom/kernel/device_tree.h
include/umicom/kernel/linker.h
include/umicom/kernel/physical_memory.h

kernel/address.c
kernel/device_tree.c
kernel/physical_memory.c

platform/qemu-riscv64/memory.c
```

## Modified source/build files

```text
CMakeLists.txt
CMakePresets.json

include/umicom/kernel/build.h
include/umicom/kernel/console.h
include/umicom/kernel/kernel.h
include/umicom/kernel/platform.h
include/umicom/kernel/riscv64/trap.h
include/umicom/kernel/types.h

kernel/main.c
```

## Documentation

```text
docs/BUILD_AND_TEST.md
docs/CMAKE_PRESET_REFERENCE.md
docs/the physical-memory foundation_DELIVERY_MANIFEST.md
docs/the physical-memory foundation_PHYSICAL_MEMORY.md
docs/ROADMAP.md
```

## Preservation statement

the first-boot and trap/timer boot, serial, trap, timer and QEMU-finisher implementations are not
deleted.

Historical short `Umi...` type/function names introduced by the first-boot and trap/timer remain
available. the physical-memory foundation establishes full `Umicom...` names for new APIs and source aliases
for the earlier symbols.

No Python, PowerShell or shell script is added by the physical-memory foundation.

## Validation performed before delivery

The supplied source was configured and cross-compiled as a RISC-V 64
freestanding ELF using Clang/LLD with the repository's strict warnings enabled.

Validation result:

```text
configure: PASS
compile:   PASS
link:      PASS
ELF:       64-bit little-endian RISC-V executable
entry:     0x80200000
```

The allocator/address logic was also exercised in an independent native host
harness (not included in the repository delivery):

```text
32,768 frame geometry
Kernel-style reservation
DTB-style reservation
allocate/free
reserved-frame refusal
overlap refusal
double-release refusal
deterministic reuse
double-free refusal
misaligned free refusal
out-of-RAM refusal
final accounting/invariant recount
```

That host harness passed.

A synthetic FDT header/range harness also passed magic, totalsize and bad-magic
checks.

The delivery environment does not contain `qemu-system-riscv64`, so **real the physical-memory foundation
QEMU runtime success is not claimed by the delivery environment**.  The user's
Windows QEMU run and CTest are the required target qualification.
