# Understanding Kernel targets and boot media

A target identifies the processor, machine, firmware and devices expected by a build. The current native Umicom Kernel implementation is for a RISC-V64 virtual machine. The repository contains `arch/riscv64` and `platform/qemu-riscv64`; its configured preset is `riscv64-clang-debug`. This source does not yet provide an x86-64 or ARM64 Kernel port, a universal USB image or an optical boot ISO.

## Inspect the supported source before choosing an image

1. Open [CMakePresets.json](../CMakePresets.json). The preset selects a toolchain and build directory.
2. Open [the RISC-V toolchain](../cmake/toolchains/riscv64-clang.cmake). It selects a freestanding RISC-V compiler target; it does not produce Windows executables.
3. Read [the Assembly bootstrap](../arch/riscv64/boot.S). The machine starts here before the C entry can use its stack and global data.
4. Read [the platform directory](../platform/qemu-riscv64). These device addresses and platform operations are specific to that machine contract.
5. Follow [the user guide](USER_GUIDE.md) for existing emulator exercises. Keep the build, guest-boot and physical-hardware evidence separate.

Changing a compiler target alone cannot port the Kernel. A new architecture also needs startup, exception handling, memory translation, interrupts, timers, context switching, native system calls and compatible device drivers. A C interface can share policy while each architecture supplies the machine-specific operations.

## Distinguish the two Umicom system projects

Umicom Kernel is the native kernel in this repository. Umicom OS is presently a separate Linux-based system project with its own RISC-V and x86-64 source recipes. Building its Linux x86-64 image does not create an x86-64 version of Umicom Kernel.

Umicom Framework's VM services can describe several guest boot protocols. An emulator launch plan is not an implementation of the selected guest, and a planned launch is not evidence of a successful boot.

## Understand the artifacts

| Artifact | What it contains | What it does not establish |
| --- | --- | --- |
| Native ELF | Machine code and load information for one target | A partitioned USB device or optical boot catalogue |
| UMCB bytecode | Portable arithmetic instructions interpreted by CVM | Firmware startup or native drivers |
| Raw disk image | Bytes representing a disk, with the structures its recipe supplies | Compatibility with every firmware or computer |
| Optical ISO | An optical filesystem and, for a bootable image, appropriate boot information | A USB disk layout merely by changing its filename |

A multi-architecture medium needs separate native loaders and matching payloads. Sharing a physical USB stick does not make an x86-64 executable run on ARM64. Even within an architecture, unsupported storage, display or input devices can prevent useful operation.

## Read a compatibility result

A useful result records the image digest, source revisions, compiler, machine model, processor features, firmware and exact observed checks. Serial output is an initial bring-up result. Normal startup, recovery, input, storage and shutdown each need their own checks. A physical machine is supported only after its named profile has evidence; a virtual-board pass is not evidence for every machine using the same CPU family.

Secure Boot checks whether firmware trusts the image's signing chain. It needs separate signing and trust qualification. This guide does not ask you to change firmware security settings or write an unqualified image to a physical disk.
