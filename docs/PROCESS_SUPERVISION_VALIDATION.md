# Umicom Kernel — Process supervision validation record

## Source baseline and preservation

The implementation is based on committed Kernel source:

```text
66896c95f3334d159737a83fd43c73a0e8fd7acc
feat(kernel): add blocking IPC and resumable message waits
```

The live GitHub commit and its tree were read before implementation. Local
source was reconstructed from the available complete-file deliveries, then the
seven build/source/test subtrees were verified byte-for-byte using Git's blob
and tree hashing against the live tree. It was not an unverified combination
of older packages and was not described as a successful network clone.

Verified baseline subtree identities:

```text
arch      492bcbd1e40ae2c2fb91802e97be97d67c3686b2
cmake     4a4b12ef4b5093706c8f20333890e22830a8ca12
include   dfa973d721c6054fb7d36aa2ffb71ee4344859c5
kernel    66613fe877d7c4aaa865f55785cd954078339b1e
platform  54fd3da5f5e7075dc8d38a04448215467a06cdfe
programs  6d388e806c051da4ae72848550d3e648f9b2c532
tests     7d73268f0174de6e57b570d8639abba1d50980bc
```

The root build files also match their recorded baseline blob identities. The
older documentation collection was not asserted to be a byte-exact Git clone;
none of those existing documents is replaced by this overlay.

| Existing file | Inserted lines | Removed or rewritten lines |
|---|---:|---:|
| `CMakeLists.txt` | 4 | 0 |
| `kernel/main.c` | 7 | 0 |
| `kernel/virtual_memory.c` | 24 | 0 |

Every existing line in these three files remains unchanged and in the original
order. The new guide explains why the additional virtual-memory accounting is
necessary. The remaining eight files are additions. No source is disabled,
removed or renamed; no compatibility aliases or scripts are added.

## Build environment

Validation used Linux x86-64 with Clang/LLD 17, GCC 14.2, CMake 3.31.6 and
Ninja 1.12.1. The Kernel CMake project selects C23; this Clang toolchain uses its
`c2x` spelling for that language mode. Both the normal Kernel and the separate
nested-fault image were cross-compiled for `riscv64-unknown-elf` with the
repository's strict warnings and `-Werror` retained.

Complete Debug and Release cross-builds passed. The independent diagnostic,
message-exchange, blocking-exchange and scheduled-work executables remain
separate build products. Repeating the incremental build reported no work.
The linked Kernel entry remains `0x80200000` and has no undefined symbols.

The completed source ZIP was also applied to a separate copy of the verified
baseline and built with spaces in both the source and build paths. This checks
that packaging contains every required file rather than relying on an extra
source in the implementation directory. It does not validate Windows execution.

**RISC-V QEMU is not installed in this validation environment. No guest run or
Windows Clang 22 result is claimed.** A successful native test is not evidence
that the RISC-V machine transition ran. The user's normal Windows build, CTest
and QEMU run remain required before runtime qualification.

## New native suite

The optional `tests/process_supervision` project passed **54 of 54 registered
cases** in each configuration:

| Compiler/configuration | Instrumentation | Result |
|---|---|---|
| Clang Debug | AddressSanitizer and UndefinedBehaviorSanitizer | 54/54 |
| Clang Release | AddressSanitizer and UndefinedBehaviorSanitizer | 54/54 |
| GCC Debug | AddressSanitizer and UndefinedBehaviorSanitizer | 54/54 |

The suite compiles the actual supervision service, scheduled-task owner, blocking
IPC layer, queues, user-memory checks, executable loader, physical allocator,
page walker, C trap dispatcher and C user-slice adapter. It reuses the existing
native scheduling fixture rather than replacing these components with a second
implementation. Hardware register observations, timer progression and the actual
RISC-V entry instruction sequence are modelled.

An additional test-only wrapper injects a chosen failed physical-frame release.
Every ordinary call still reaches the real allocator. The loader, destructor,
bitmaps and page tables are not mocked. Failure is injected before that selected
release changes its bitmap, matching the allocator's refusal contract.

Coverage includes parent/sibling/ancestor authority, terminal collection,
adoption, descendant cancellation before adoption, unrelated-peer survival,
blocked and paused cancellation, endpoint closure, stable completion records,
admission shutdown, stale generations, capacity, call/slice budgets and entry
refusal. There are **1,000 successive task lifetimes** and **24 available-memory
budgets** for failed admission and rollback.

Unsafe-return and deliberately corrupted-owner cases intentionally retain their
model frames. Retention is the expected safety behaviour, not a successful
reclamation claim. Normal recoverable cases finish with empty task ownership,
no pending waits or message references and allocator accounting restored.

## A reproduced partial-cleanup defect

Before the virtual-memory additions, the new suite passed 53 of 54 tests.
`partial-cleanup-retry` reproduced a failure to collect after a partial teardown.
The first release could succeed, but the address-space counters still described
the removed child table and its mappings. A retry then failed the existing
independent page-table validation before it could finish releasing the owner.

The fix counts a retiring leaf table's mappings while the table is still owned.
After each successful free and the original edge removal, the corresponding
table and mapping counts are adjusted. A failed release leaves the edge and
counts untouched. The original full-success reset is retained unchanged.

The corrected test injects one failed release at each release position of a
representative loaded image. It checks that the supervisor's output remains
unchanged, the terminal report remains readable and a later collection succeeds
without resetting the owner or losing frames. The unpublished failed-load
rollback test exercises the separate retained-owner recovery path.

After these changes the new suite passed 54/54 in all three configurations.
This is bounded fault-injection coverage, not a proof against every possible
hardware, memory-corruption or allocator implementation failure.

## Existing native regressions

All suites were rebuilt using the updated page-table implementation:

| Existing suite | Result | Instrumentation |
|---|---:|---|
| Blocking IPC | 49/49 | AddressSanitizer and UndefinedBehaviorSanitizer |
| User scheduling | 53/53 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Message channels | 72/72 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Process registry, including the real diagnostic ELF input | 49/49 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Executable loading, including the real blocking ELF input | 68/68 | AddressSanitizer and UndefinedBehaviorSanitizer |
| User memory | 24/24 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Trap integrity | 70/70 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Interrupt ownership | 68/68 | 64 policy cases with both sanitizers; 4 alternate-stack cases with UBSan only |
| Cooperative threads | 55/55 | UndefinedBehaviorSanitizer |
| Events | 48/48 | UndefinedBehaviorSanitizer |

The alternate-stack adapter does not implement AddressSanitizer's fibre-switch
notifications. No AddressSanitizer result is claimed for those cases. The host
ELF-input tests inspect and load RISC-V bytes; they do not execute those bytes.

## Current-image fixture check

CTest registration contains 19 main tests, including
`kernel.riscv64.process_supervision`, which requires `kernel_current_image`.

In a disposable source/build copy, a compiler error was deliberately inserted.
A known earlier Kernel ELF was left in the output directory. Selecting only the
new runtime test caused the fixture to build first, report the compiler failure
and mark the dependent runtime test **Not Run**. The seeded ELF's checksum stayed
unchanged. The rejected source and error injection are not in the delivery.

That registration-only copy used `/usr/bin/false` as a non-emulator sentinel.
It was not used to claim a QEMU result: the failed fixture prevented execution
of the dependent command. The actual user configuration still locates QEMU
through the unchanged repository logic.

CTest fixture semantics are documented by Kitware:
https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html

## Guest acceptance still required

The new guest test reuses existing loaded diagnostic and blocking-message ELFs.
Its six cases exercise parent authority, faulted-parent subtree cancellation,
adoption of a blocked child, an explicit subtree stop, producer/consumer lifetime
and shutdown with retained terminal evidence. The original ECALL handler,
machine control state and physical-frame counts are checked afterwards.

Expected final evidence is:

```text
process-supervision.original-trap-handler=pass
process-supervision.machine-state=restored
process-supervision.frame-accounting=restored
process-supervision.completed-cases=6
process-supervision-test=pass
UMICOM_KERNEL_PROCESS_SUPERVISION_READY
UMICOM_KERNEL_END
```

Only an actual successful build and guest execution may qualify those markers.
A source manifest, native count or ELF header alone cannot do that.
