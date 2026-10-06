# Umicom Kernel — Managed-service validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Exact starting source

This complete-file overlay starts from
`dee75f562886f2483503c6673be2b85ea8c852cf` of
`umicom-foundation/umicom-kernel`, containing normal startup, one-shot boot
services and independent recovery. Build-relevant baseline directory trees and
root build files were reconstructed and checked against their Git tree/blob
identities before editing.

| Existing file | Insertions | Removals or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| include/umicom/kernel/user_scheduler.h | 2 | 0 |
| kernel/console_runtime.c | 13 | 0 |
| kernel/console_shell.c | 20 | 0 |
| kernel/main.c | 6 | 0 |
| kernel/user_monitor.c | 11 | 0 |
| kernel/user_scheduler.c | 32 | 0 |
| Total | 88 | 0 |

Every old line remains in its original order. The main include insertion leaves
the old trap-header explanation immediately next to its original include.
No implementation is superseded, so no disabled historical block is necessary.
All platform and Assembly files, the one-shot controller, normal boot, recovery,
allocators, filesystem, IPC, process supervisor and existing tests are unchanged.

## Toolchain and instrumentation boundary

The local environment is Linux x86-64 with Clang/LLD 17.0.0, GCC 14.2.0,
CMake 3.31.6 and Ninja 1.12.1. Cross-builds use the existing RV64 unknown-ELF
integer ABI and warning-as-error options. Clang 17 uses the C23 draft flag chosen
by CMake. No warning suppression or hosted runtime is added to the Kernel.

RISC-V QEMU is unavailable in this environment. The actual guest privilege
transitions, new report instruction wrapper and interactive terminal were not
executed here. The Windows compiler reported in the project is newer than the
available cross-compiler; no Windows Clang pass is claimed.

## Cross-builds and packaging

The complete Debug and Release sources compile and link the ordinary Kernel,
interactive console, normal system, recovery profiles, independent nested-fault
image and all separate native programs, including `umicom-health-service.elf`.
The Kernel entry remains `0x80200000`; the health program entry is `0x00400000`.
Repeating either completed build reports no work to do.

The finished ZIP is applied to an independent copy of the verified baseline in
source and build paths containing spaces. That source compiles and links without
using working-tree-only files. Its native managed-service suite also compiles and
runs from that extracted source.

The new CTest is `kernel.riscv64.managed_services`; the complete main registration
contains 31 tests with QEMU discovery enabled. The same current-image fixture
remains required. A deliberately invalid C token in an isolated packaged copy
makes that fixture fail and the dependent test Not Run. Existing linked image
hashes stay unchanged. The invalid token is removed after the experiment and is
not present in the ZIP.

A non-emulator executable is supplied only for CTest registration and this
failure-dependency experiment, never for a guest success claim. No fake runtime
output or substitute pass marker is used.

## New native suite: 77 tests

All 77 pass in Clang Debug, Clang Release and GCC Debug with AddressSanitizer and
UndefinedBehaviorSanitizer enabled. The suite links the actual new controller,
report hook, scheduler additions, console adapter and guest C validation, plus
the actual existing supervisor, launch packer, streams, IPC, loader, allocator,
page walker and trap dispatcher.

The established synthetic ELF and machine-entry model supply observations at
the architecture boundary. The test-only free wrapper can refuse a selected
frame release while all normal releases still call the actual allocator.

The cases cover:

- Explicit READY versus process entry/exit; heartbeat renewal; retained
  continuation; parked tasks consuming no extra user dispatches.
- Startup and health deadlines, late reports, sequence replay, duplicated READY,
  invalid operations, reserved registers, copied sessions and unbound reports.
- Dependency ordering, out-of-order definitions, transitive revocation, fresh
  identities, optional failure, controlled restart and unchanged budget limits.
- Cleanup failure and retry, no replacement before collection, retained evidence,
  shutdown, reversed/overflowing time, refused entry and unsafe return.
- Full plan validation, including disconnected cycles and every timing/admission
  bound, plus 200 successive fresh manager/task lifetimes.
- Eight actual console-adapter tests: unattached status, invalid mode, real
  controller observations, restart, stop/report retention, console selection,
  partial-close retry and malformed restart indices. They call the real existing
  tokenizer, not a replacement parser.
- One run of the exact guest C acceptance sequence with explicitly modelled
  time, native programme modes and trap observations. It checks the full
  controller sequence but does not execute RISC-V instructions.

The guest sequence's silent-provider case gives its healthy consumer a reporting
interval long enough that the unchanged syscall budget cannot expire first.
That distinction matters: a heartbeat-deadline test must not accidentally pass
or fail because an unrelated healthy consumer consumed its call budget.

The corrupt-return case intentionally retains its live ownership instead of
force-freeing an unverified context. Successful-lifetime cases check allocator
accounting; unsafe retention is not misrepresented as ordinary cleanup.

## Existing native regression projects

All nineteen existing projects pass on the new source:

| Project | Passed |
|---|---:|
| Blocking IPC | 49/49 |
| One-shot boot services and recovery support | 88/88 |
| Console shell | 65/65 |
| Cooperative events | 48/48 |
| Executable loader | 68/68 |
| File services | 54/54 |
| Interrupt ownership | 68/68 |
| Mapped regions | 59/59 |
| Message channels | 72/72 |
| Object caches | 65/65 |
| Process registry | 49/49 |
| Process supervision | 54/54 |
| Structured launch | 57/57 |
| VFS/RAMFS | 66/66 |
| Standard streams | 67/67 |
| Cooperative threads | 55/55 |
| Trap integrity | 70/70 |
| User memory | 24/24 |
| User scheduling | 53/53 |

The loader, registry and VFS bridge receive the actual newly cross-linked health
ELF as input. These validate parsing, copying, ownership and file-to-loader
behaviour; they do not execute its RISC-V code on the host.

Clang Debug sanitizer options are enabled. Existing alternate-stack thread/event
cases retain their documented UBSan-only boundary; no ASan fibre-switch support
is invented or claimed for them. All existing tests remain unchanged.

## Guest qualification still required

The six new guest cases run through the real native health executable and
existing Assembly when tested under the owner's RISC-V QEMU:

1. Two dependent live services both issue READY and subsequent heartbeats.
2. Restart revokes the consumer and collects both old instances before new
   identities are admitted.
3. A silent provider loses its health lease and the consumer's readiness.
4. A provider which never reports READY cannot release its dependent.
5. Exit value zero does not count as a healthy live service.
6. A deliberate fault and normal finite-budget exhaustion remain terminal.

The harness then checks the original ECALL handler, selected machine control
registers and physical allocation/free/reservation accounting. The recorded
checkpoint count can vary with clock polling and is not a fixed acceptance value.

Expected final markers:

```text
managed-services.original-trap-handler=pass
managed-services.machine-state=restored
managed-services.frame-accounting=restored
managed-services.completed-cases=6
managed-services-test=pass
UMICOM_KERNEL_MANAGED_SERVICES_READY
```

Those are expectations for a future guest run, not fabricated local output.
Normal startup and forced recovery do not automatically launch this diagnostic
service domain. The interactive `daemons` commands explicitly opt into it.
