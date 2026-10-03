# Umicom Kernel Roadmap

Status baseline: 1 October 2026

The roadmap is evidence-gated.  Source presence does not complete a milestone;
the stated build/runtime evidence must pass.

## Repository and architecture foundation — COMPLETE

Delivered:

- independent `umicom-kernel` repository;
- architecture and decision register;
- originality/reuse policy;
- native ABI/portability direction;
- promotion gates;
- main-only repository workflow.

## RISC-V freestanding boot — COMPLETE

Qualified on QEMU RISC-V `virt`:

- Assembly `_start`;
- bootstrap stack;
- BSS clearing;
- C23 `UmiKernelMain`;
- polling NS16550 serial output;
- QEMU firmware hand-off with `-bios`;
- test-finisher clean exit;
- real CTest QEMU boot.

The first-boot path remains a regression test as later capabilities are added.

## Machine traps and timer — COMPLETE

Deliver:

- direct-mode `mtvec`;
- complete integer trap frame;
- Assembly save/restore + `mret`;
- C23 trap cause dispatcher;
- deliberate M-mode ECALL exception;
- QEMU CLINT machine timer;
- real machine-timer interrupt;
- first-boot regression + trap/timer QEMU acceptance.

Exit:

- ECALL cause 11 handled and returns after the instruction;
- machine-timer cause 7 handled and returns;
- one-shot timer cannot remain pending;
- first-boot and trap/timer CTests both pass.

## K3 — Physical memory foundation — CURRENT BATCH

Deliver:

- full `Umicom...` naming convention for Kernel APIs with legacy short aliases retained;
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
- first-boot + trap/timer + physical-memory real-QEMU regression tests.

Exit:

- Kernel and DTB pages are unavailable to ordinary allocation;
- 128 MiB / 4 KiB geometry reports 32,768 physical frames;
- two simultaneous allocations are distinct and page aligned;
- a released page can be reserved/released and reused deterministically;
- reserved-frame free, overlapping reserve, double free, double release,
  misaligned free, address overflow and out-of-RAM reserve are refused;
- final allocation/reservation accounting returns to the post-bootstrap baseline;
- allocator invariant recount passes;
- first-boot, trap/timer and physical-memory CTests all pass.

The physical-memory subsystem remains the allocation authority beneath virtual
memory.

## Sv39 virtual memory foundation — CURRENT

Deliver:

- three-level Sv39 page-table construction;
- 4 KiB leaf mappings;
- canonical-address validation;
- permission validation;
- software page-table walking and translation;
- duplicate-map refusal;
- unmapping with empty intermediate-table reclamation;
- page-table ownership accounting;
- physical-frame leak checks;
- CPU translation deliberately remains disabled until a later activation step.

Exit:

- one root and the required intermediate tables are allocated through the
  physical-memory authority;
- a mapped virtual address translates to the expected physical frame and byte
  offset;
- duplicate mappings, non-canonical addresses and invalid permissions are
  refused explicitly;
- unmapping removes translation and reclaims empty intermediate tables;
- destroying the address space releases all page-table frames without freeing
  separately owned mapped data frames;
- physical-memory accounting returns exactly to the pre-test baseline;
- all descriptive real-QEMU capability tests pass.

## Threads and scheduler

Planned:

- kernel thread representation;
- context switch;
- pre-emption;
- thread states;
- wait/sleep;
- deterministic scheduler evidence.

## Kernel objects, handles and capabilities

Planned:

- typed object base;
- per-process handle table;
- rights/capabilities;
- duplication/restriction;
- explicit object lifetime.

## IPC and shared memory

Planned:

- channels;
- messages;
- events/waits;
- shared-memory objects;
- peer-death/deadline semantics.

## User mode and native ABI

Planned:

- privilege transition;
- user address spaces;
- syscall dispatch;
- ELF user-process loader;
- first user-mode Umicom program.

## VFS and RAM filesystem

Planned:

- VFS object contracts;
- initramfs/RAMFS;
- path handling;
- file handles;
- directory enumeration.

## VirtIO storage and persistence

Planned:

- VirtIO transport/block;
- persistent filesystem integration;
- flush/error semantics;
- reboot persistence test.

## Network foundation

Planned:

- VirtIO network;
- user-space network-service boundary;
- first real packet path.

## Umicom System Manager

Planned:

- Master Controller / Slave Controller system-service lifecycle;
- service dependencies;
- health/shutdown;
- recovery mode.

## POSIX compatibility foundation

Planned:

- defined compatibility subset;
- file/process/thread/time/memory compatibility;
- first conventional free-software program.

## Framework native adapter

Planned:

- Umicom System Services native adapter;
- first real Framework example in native-kernel user space.

## Graphics / Desk research

Only after the user/process/system-service foundations are credible.

## Additional architectures

x86-64 second, then ARM64 after common contracts are mature.

## Production promotion programme

Begins only after the explicit promotion gates can be evaluated honestly.
