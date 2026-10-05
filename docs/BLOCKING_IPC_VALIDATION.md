# Blocking IPC — Validation and delivery boundaries

## Source baseline and preservation

Repository: umicom-foundation/umicom-kernel
Baseline commit: a68fc972d4815d0e41b0975315dc84d6543fa852

The seven source/build subtrees (arch, cmake, include, kernel, platform,
programs and tests) were reconstructed and checked against the committed Git
tree hashes. Existing historical documentation is retained, not used as a claim
of current runtime completion. The replacement audit compares exact committed
bytes for every modified file. Changes are insertion-only:

| File | Added | Removed or rewritten |
|---|---:|---:|
| `CMakeLists.txt` | 4 | 0 |
| `include/umicom/kernel/user_scheduler.h` | 10 | 0 |
| `kernel/main.c` | 7 | 0 |
| `kernel/user_monitor.c` | 12 | 0 |
| `kernel/user_scheduler.c` | 74 | 0 |

No original implementation is superseded. No code needs disabling, no file is
deleted or renamed, and no existing comment is rewritten. There are no new
PowerShell, Python or shell scripts. Internal delivery tooling is not included.

## Tools used locally

Linux x86-64; Clang/LLD 17.0.0; GCC 14.2.0; CMake 3.31.6; Ninja 1.12.1.
This is not validation with the Windows Clang 22 toolchain used by the project
owner. RISC-V QEMU is unavailable in this environment.

## Executed checks

Complete Debug and Release RISC-V cross-builds passed, including the independent
blocking-message executable, embedded carrier, normal Kernel and the existing
nested-fault test image. The Kernel entry remains 0x80200000. The independent
program entry is 0x00400000. Its load segments are RX, R and RW, never RWX.
Repeated Debug and Release builds both reported no work to do.

The new native suite passed in Clang Debug, Clang Release and GCC Debug with
AddressSanitizer and UndefinedBehaviorSanitizer. It links the actual C wait
service, scheduler, slice adapter and policy, trap dispatcher, queues, service
binding, executable loader, allocator, page walker and checked user copying.
Only privileged entry and CSR/timer observations are modelled. The existing
scheduler test's models are reused under a test-local entry name; that original
test source is unchanged.

| Suite/configuration | Passed |
|---|---:|
| new-clang-debug | 49/49 |
| new-clang-release | 49/49 |
| new-gcc-debug | 49/49 |
| user_scheduling | 53/53 |
| message_channels | 72/72 |
| process_registry | 49/49 |
| executable | 68/68 |
| user_memory | 24/24 |
| trap_integrity | 70/70 |
| interrupts | 68/68 |
| threads | 55/55 |
| events | 48/48 |

The new suite includes 1,000 successive load/block/complete/cancel/reap lifetimes;
empty/full queues; rights and owner checks; source snapshots; zero, finite and
infinite waits; registered deadline ordering; FIFO among existing waits;
peer cancellation, faults, exit and budget exhaustion; stale slots; invalid
second destination pages; no partial write/consume; exact call-count/PC
preservation; binding refusal and cleanup; and deliberately corrupted retained
ownership. Poisoned-state tests retain memory rather than forcing unsafe frees.
These are bounded regression tests, not exhaustive proofs or general fuzzing.

Instrumentation for existing regressions follows their unchanged test projects.
Threads and events use UndefinedBehaviorSanitizer only with their alternate
host-stack adapter. Interrupt policy tests use ASan/UBSan, while their separate
alternate-stack scheduler tests use UBSan. No ASan fibre-support claim is made.
Other listed ordinary native suites use their ASan/UBSan configuration.

A separate dependency-only CTest configuration registered eighteen tests. It
used /bin/false as a deliberately non-emulating placeholder and injected a
compile error into a disposable source copy. The current-image fixture failed,
the new dependent test was NOT RUN, and the seeded older ELF was unchanged.
No placeholder invocation is counted as emulator evidence.

The finished ZIP is also applied to a separate baseline copy with spaces in
both source and build paths and built before final publication. This checks the
actual overlay and embedding dependencies, not just the original work tree.

## Runtime qualification still required

Neither the new RISC-V Assembly program entry nor its ECALL/resumption path was
executed locally. Native models and successful links do not prove a guest boot.
Run the normal Windows build, all eighteen main CTests and the explicit QEMU
command. Expected normal guest completion includes:

```text
blocking-ipc.messages-received=12
blocking-ipc.resume-without-restarting=pass
blocking-ipc.deadline-without-user-spin=pass
blocking-ipc.peer-terminal-cleanup=pass
blocking-ipc.original-trap-handler=pass
blocking-ipc.machine-state=restored
blocking-ipc.frame-accounting=restored
blocking-ipc.completed-cases=5
blocking-ipc-test=pass
UMICOM_KERNEL_BLOCKING_IPC_READY
UMICOM_KERNEL_END
```

The check count and addresses are not fixed acceptance values. Additional static
service storage increases the protected Kernel range. All five guest cases must
pass and QEMU must exit zero before committing the update.

## Deferred work

This is not Kernel pre-emption, SMP-safe IPC, a hardware idle loop, arbitrary
user wait-any, shared memory, priorities or a process-registry syscall extension.
Private scheduler-domain endpoint cleanup is implemented; general parent/service
relationships and cross-subsystem process supervision remain subsequent work.
