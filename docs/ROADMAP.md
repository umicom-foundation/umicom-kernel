# Umicom Kernel Roadmap

## K0 — Repository and architecture foundation
Independent repository, documentation, decision register, promotion gates and build policy.

## K1 — RISC-V freestanding boot
QEMU RISC-V `virt`, Assembly entry, C23 kernel entry, serial console, build identity and smoke test.

## K2 — HAL, traps and timers
Architecture descriptor, trap/exception entry, interrupts, timer and panic/report path.

## K3 — Memory
Frame/page allocator, kernel mappings, address-space object, checked ranges and accounting.

## K4 — Threads and scheduler
Kernel threads, context switch, pre-emption, thread states, wait/sleep and teardown.

## K5 — Objects, handles and capabilities
Typed object model, per-process handle tables, rights masks, duplication/restriction and lifetime rules.

## K6 — IPC/shared memory
Channels, events, shared memory, bounded messages, deadlines and peer-death semantics.

## K7 — User mode and native ABI
Privilege transition, user address spaces, syscall dispatch, ELF loader and first user-mode process.

## K8 — VFS/RAMFS
VFS object contract, initramfs/RAMFS, paths, directory enumeration and file handles.

## K9 — VirtIO storage and persistence
VirtIO block plus simple persistent filesystem integration, flush/error semantics and reboot persistence test.

## K10 — Network foundation
VirtIO network plus user-space network-service boundary.

## K11 — Umicom System Manager
Master Controller / Slave Controller service lifecycle, dependencies, health, shutdown and recovery mode.

## K12 — POSIX compatibility foundation
File/process/thread/time/memory compatibility subset and first conventional free-software program.

## K13 — Framework native adapter
Umicom System Services adapter and first real Framework example running in native-kernel user space.

## K14 — Graphics/Desk research
Display/input service boundary and actual graphical guest evidence.

## K15 — Additional architectures
x86-64, then ARM64.

## K16 — Promotion programme
Run the explicit production gates.
