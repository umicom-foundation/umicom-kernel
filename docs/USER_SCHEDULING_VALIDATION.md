# Umicom Kernel — User-scheduling validation record

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Source basis and preservation

The source overlay starts from the committed interrupt-ownership state:

```text
1baeb8d5ab7e8c4d2e8af15a814faf7ca0c58a96
feat(kernel): add interrupt ownership and nested critical sections
```

The four replaced complete files were compared against these exact Git blobs:

| Existing file | Baseline Git blob | Added lines | Removed/rewritten lines |
|---|---|---:|---:|
| `CMakeLists.txt` | `124090db6d24879d14acf75bad78492d9408ddc4` | 4 | 0 |
| `arch/riscv64/user_execution.S` | `a04eda86535739750165f0e1c80683a20d774fda` | 84 | 0 |
| `kernel/main.c` | `1067bcf963130c1d6274544af7407cd74e4c0f99` | 7 | 0 |
| `kernel/user_monitor.c` | `b88291349f5b208711fe11c8565951bf94794c57` | 13 | 0 |

The comparison checks each original line and its order, not only line counts.
Every existing comment and statement in those files is retained. There are no
renames, deleted files, new compatibility aliases, suppressed warnings or delivered
Python/PowerShell/shell scripts. No implementation is superseded or disabled.
The remaining eighteen delivery files are new implementation, program, build,
native-test and documentation files.

## Validation environment

* Linux x86-64 host.
* Clang 17.0.0 and LLD 17.0.0, targeting `riscv64-unknown-elf`.
* GCC 14.2.0 for the independent native policy build.
* CMake 3.31.6 and Ninja 1.12.1.
* RISC-V QEMU is not installed in this validation environment.

The Windows development compiler previously reported by the project owner is
Clang 22.1.8. That compiler was not available here. A successful older cross-build
is not represented as validation of every newer diagnostic or Windows runtime.

## Cross-build and packaging checks

The complete Kernel, its separate nested-trap test image and all independently
linked native programs compiled and linked in Debug and Release with the existing
strict warning flags. The existing `-Werror` policy remains enabled.

The Kernel ELF entry remains `0x80200000`. The new diagnostic ELF has entry
`0x00400000` with separate RX text, read-only data and RW private observation
storage. Its writable observation page is at `0x00600000`; that address is a
validation contract only, not a new syscall or general process ABI.

The finished delivery archive is applied to a separate baseline copy and rebuilt
in source/build paths containing spaces. Repeating its incremental build must
report no work to do. These checks exercise the embedded-file dependency and
path quoting, not actual instruction execution.

## Native user-scheduling suite

The fifty-three registered cases passed in each of:

* Clang Debug with AddressSanitizer and UndefinedBehaviorSanitizer;
* Clang Release with AddressSanitizer and UndefinedBehaviorSanitizer;
* GCC Debug with AddressSanitizer and UndefinedBehaviorSanitizer.

The suite compiles the actual new scheduler and C architecture adapter,
`user_slice.c`, the original C syscall dispatcher, executable inspector, process
owner, physical allocator, page walker and checked user-copy implementation.
Only the CSR/timer primitives, ownership-admission observation, instruction-cache
primitive and Assembly user execution call are replaced with explicit host models.

A model invocation can produce a timer, syscall, fault, refused entry or broken
restoration. It does not execute the diagnostic's RISC-V instructions. In
particular, a host test which checks all integer frame slots is not a substitute
for the guest Assembly register-sentinel probes.

Coverage includes:

| Area | Evidence exercised |
|---|---|
| Admission | Null/copy/reinitialisation refusal, capacity, malformed ELF, identity exhaustion and invalid budgets. |
| Continuations | Two roots, round-robin selection, every integer frame slot, unchanged interrupted PC, syscall PC/result handling and zero user stack. |
| Lifetime | Pinning across pauses, lower destructor refusal, explicit cancel/reap, stale and forged tokens, generation retirement and 1,000 successive lifetimes. |
| Budgets | Slice exhaustion, one fault without losing the neighbour, exit terminality and the existing call budget across 33 model invocations. |
| Entry policy | Managed ownership refusal, wrong hart, live interrupt/translation/PMP/extended state, busy timer, overflowing deadline and hardware admission refusal. |
| Saved-state safety | Privileged/odd/unmapped/reserved/extended-state frames and wrong XLEN are refused. |
| Trap binding | Wrong/recursive session binding, early timer retry and bounded early-interrupt storm. |
| Restoration | Corrupted control/counter/compare state prevents continued dispatch or unsafe destruction. |
| Allocation | Twenty-five free-memory budgets, from zero through twenty-four available frames, exercise ordinary load rollback. |

## Existing native regression results

The following suites were rebuilt after the source integration and passed:

| Suite | Passed | Instrumentation |
|---|---:|---|
| Interrupt ownership | 68/68 | 64 policy cases with ASan/UBSan; 4 alternate-stack cases with UBSan. |
| Trap integrity | 70/70 | ASan/UBSan. |
| Cooperative threads | 55/55 | UBSan with the existing native alternate-stack adapter. |
| Events | 48/48 | UBSan with the existing native alternate-stack adapter. |
| Message channels | 72/72 | ASan/UBSan. |
| Process registry | 49/49 | ASan/UBSan, including loading the real new diagnostic ELF without executing it. |
| Executable loader | 68/68 | ASan/UBSan, including inspecting/copying the real new diagnostic ELF. |
| User memory | 24/24 | ASan/UBSan. |

No ASan claim is made for existing alternate-stack adapters which do not implement
ASan fibre-switch notifications. Models of hardware are identified as models,
not reported as real guest observations.

## Current-image fixture negative test

In a disposable validation copy, the complete Kernel was first built and hashed.
An intentional `#error` was then appended to a new C source. Running only the new
user-scheduling test selected its required build fixture automatically.

The observed result was:

```text
kernel.build.current                 Failed
kernel.riscv64.user_scheduling        Not Run
```

The previously linked ELF remained byte-for-byte unchanged. A deliberately inert
executable path was used solely to register tests for this dependency check; it
was not used as an emulator and no runtime pass was inferred from it.

The configured suite contains seventeen main tests. The new test requires
`kernel_current_image`, with a thirty-second timeout. Existing tests and their
properties remain unchanged.

## Required guest qualification

The following are expected checks, not locally observed QEMU passes:

1. Both independently loaded CPU-bound programs make progress and each records
   at least two actual machine-timer pre-emptions before its test gate opens.
2. Every integer register sentinel and nested C local survives the pauses.
3. Timer `mepc` is retained, the program starts once, and A's writable page does
   not change B's private page at the same virtual address.
4. Normal COPY/IDENTITY/EXIT calls still work after resumption.
5. A read-only fault ends only its task; an endless loop exhausts its slice
   budget; a zero-stack EXIT returns safely.
6. Paused cancellation, stale-token refusal and cumulative syscall limits work.
7. A managed critical section refuses dispatch before timer or task mutation.
8. Machine control state and the parked timer return after every invocation;
   the original ECALL handler and final frame-accounting checks remain healthy.

Run the normal Windows configure, build, CTest and QEMU commands in
`TIMER_DRIVEN_USER_SCHEDULING.md`. Successful guest output must include:

```text
UMICOM_KERNEL_USER_SCHEDULING_READY
UMICOM_KERNEL_END
```

with a zero QEMU exit code. Save the host compiler/emulator versions with any
qualification transcript. Neither a Git push nor a successful cross-link proves
that these runtime properties hold.

## Remaining boundaries

The new scheduler is a bounded single-hart Kernel service for integer user
contexts. It does not pre-empt Kernel code, provide SMP or realtime guarantees,
expose scheduler handles to users, automatically coordinate existing registry
handles, or turn message services into blocking operations. The existing
cooperative callbacks/events keep their original contract. No production sandbox,
interactive OS or complete scheduling subsystem is claimed by these tests.
