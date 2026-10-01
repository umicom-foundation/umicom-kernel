# K2 Delivery Manifest

Delivery: RISC-V machine trap, synchronous ECALL and machine-timer interrupt foundation  
Prepared against: `eb4e2dbd161067702aaaed11e2a8c34d9e7dfd06`  
Date: 1 October 2026

## Repository files delivered

Modified complete files:

```text
CMakeLists.txt
CMakePresets.json
include/umicom/kernel/build.h
include/umicom/kernel/platform.h
kernel/main.c
platform/qemu-riscv64/finish.c
docs/CMAKE_PRESET_REFERENCE.md
docs/ROADMAP.md
```

New complete files:

```text
arch/riscv64/trap.S
arch/riscv64/trap.c
include/umicom/kernel/riscv64/trap.h
platform/qemu-riscv64/timer.c
docs/K2_TRAPS_TIMER.md
docs/K2_WINDOWS_BUILD_AND_TEST.md
docs/K2_DELIVERY_MANIFEST.md
```

No existing source file is deleted.

No Python, PowerShell or shell script is added.

## Preserved K1 source

The K2 source list continues to use the existing:

```text
arch/riscv64/boot.S
arch/riscv64/linker.ld
kernel/console.c
platform/qemu-riscv64/serial.c
include/umicom/kernel/types.h
include/umicom/kernel/kernel.h
include/umicom/kernel/console.h
cmake/toolchains/riscv64-clang.cmake
```

K2 deliberately does not replace those working K1 implementations merely to
create a larger delivery.

## Build validation performed before delivery

The delivered source was cross-compiled with:

```text
Clang 17
CMake 3.31.6
Ninja 1.12.1
target: riscv64-unknown-elf
```

Result:

```text
configure: PASS
compile:   PASS
link:      PASS
ELF:       RISC-V 64-bit little-endian executable
entry:     0x80200000
```

The validation environment did not contain `qemu-system-riscv64`, so no QEMU
runtime PASS is claimed by the delivery author.

The recipient's real QEMU K1/K2 CTests are the runtime acceptance gate.

## Expected commit

After both QEMU tests pass:

```text
feat(kernel): add RISC-V trap and timer foundation
```
