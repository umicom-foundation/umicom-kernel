# Umicom Kernel Roadmap

Status baseline: 1 October 2026

The roadmap is evidence-gated.  Source presence does not complete a milestone;
the stated build/runtime evidence must pass.

## K0 — Repository and architecture foundation — COMPLETE

Delivered:

- independent `umicom-kernel` repository;
- architecture and decision register;
- originality/reuse policy;
- native ABI/portability direction;
- promotion gates;
- main-only repository workflow.

## K1 — RISC-V freestanding boot — COMPLETE

Qualified on QEMU RISC-V `virt`:

- Assembly `_start`;
- bootstrap stack;
- BSS clearing;
- C23 `UmiKernelMain`;
- polling NS16550 serial output;
- QEMU firmware hand-off with `-bios`;
- test-finisher clean exit;
- real CTest QEMU boot.

K1 remains a regression test in later milestones.

## K2 — HAL traps and machine timer — COMPLETE

Deliver:

- direct-mode `mtvec`;
- complete integer trap frame;
- Assembly save/restore + `mret`;
- C23 trap cause dispatcher;
- deliberate M-mode ECALL exception;
- QEMU CLINT machine timer;
- real machine-timer interrupt;
- K1 regression + K2 QEMU acceptance.

Exit:

- ECALL cause 11 handled and returns after the instruction;
- machine-timer cause 7 handled and returns;
- one-shot timer cannot remain pending;
- K1 and K2 CTests both pass.

## K3 — Physical memory foundation — CURRENT BATCH

Deliver:

- full `Umicom...` naming convention for new Kernel APIs with K1/K2 aliases retained;
- checked physical-address addition/alignment;
- fixed QEMU 128 MiB RAM profile behind the platform boundary;
- minimal FDT header validation so DTB pages can be protected;
- linker Kernel-range protection;
- 4 KiB physical page/frame decision;
- bounded dual-bitmap frame allocator;
- explicit reserve/release operations;
- reserved-frame and overlap protection;
- misaligned/out-of-RAM refusal;
- deterministic frame reuse;
- double-free and double-release refusal;
- independent bitmap/accounting invariant validation;
- K1 + K2 + K3 real-QEMU regression tests.

Exit:

- Kernel and DTB pages are unavailable to ordinary allocation;
- 128 MiB / 4 KiB geometry reports 32,768 physical frames;
- two simultaneous allocations are distinct and page aligned;
- a released page can be reserved/released and reused deterministically;
- reserved-frame free, overlapping reserve, double free, double release,
  misaligned free, address overflow and out-of-RAM reserve are refused;
- final allocation/reservation accounting returns to the post-bootstrap baseline;
- allocator invariant recount passes;
- K1, K2 and K3 CTests all pass.

This remains physical memory only.  K3 does not create page tables or virtual
address spaces.

## K4 — Threads and scheduler

Planned:

- kernel thread representation;
- context switch;
- pre-emption;
- thread states;
- wait/sleep;
- deterministic scheduler evidence.

## K5 — Kernel objects, handles and capabilities

Planned:

- typed object base;
- per-process handle table;
- rights/capabilities;
- duplication/restriction;
- explicit object lifetime.

## K6 — IPC and shared memory

Planned:

- channels;
- messages;
- events/waits;
- shared-memory objects;
- peer-death/deadline semantics.

## K7 — User mode and native ABI

Planned:

- privilege transition;
- user address spaces;
- syscall dispatch;
- ELF user-process loader;
- first user-mode Umicom program.

## K8 — VFS and RAM filesystem

Planned:

- VFS object contracts;
- initramfs/RAMFS;
- path handling;
- file handles;
- directory enumeration.

## K9 — VirtIO storage and persistence

Planned:

- VirtIO transport/block;
- persistent filesystem integration;
- flush/error semantics;
- reboot persistence test.

## K10 — Network foundation

Planned:

- VirtIO network;
- user-space network-service boundary;
- first real packet path.

## K11 — Umicom System Manager

Planned:

- Master Controller / Slave Controller system-service lifecycle;
- service dependencies;
- health/shutdown;
- recovery mode.

## K12 — POSIX compatibility foundation

Planned:

- defined compatibility subset;
- file/process/thread/time/memory compatibility;
- first conventional free-software program.

## K13 — Framework native adapter

Planned:

- Umicom System Services native adapter;
- first real Framework example in native-kernel user space.

## K14 — Graphics / Desk research

Only after the user/process/system-service foundations are credible.

## K15 — Additional architectures

x86-64 second, then ARM64 after common contracts are mature.

## K16 — Production promotion programme

Begins only after the explicit promotion gates can be evaluated honestly.
