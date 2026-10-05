# Umicom Kernel — Object-cache validation record

## Source basis and scope

Repository baseline: `23aa501b63905752a815d31f965a15e922461021`.
Its commit is `feat(kernel): add process supervision and coordinated lifetime cleanup`.

The source overlay changes four existing files, only by insertion:

| File | Added | Removed or rewritten |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| include/umicom/kernel/physical_memory.h | 15 | 0 |
| kernel/main.c | 7 | 0 |
| kernel/physical_memory.c | 26 | 0 |
| Total | 52 | 0 |

The physical allocator receives only an observation API which reuses its private
address-validation and bitmap readers. None of its allocation, reservation,
release or existing validation logic changes. No service storage is migrated.
The source-preservation review accompanying this delivery contains full-file
hashes and the complete existing-file comparison.

The build-source baseline was reconstructed from the conversation's source
packages, reconciled with the live repository and verified using Git tree
identities for `arch`, `cmake`, `include`, `kernel`, `platform`, `programs` and
`tests`. The root CMake file and presets were also checked. Historical documents
outside the overlay are not represented as a fresh full-repository audit.

## Build checks

Complete Debug and Release RV64 builds compile the current Kernel, all existing
independent user programs, the new object cache and the nested-fault image.
Strict existing warning options, including `-Werror`, remain unchanged.
The normal ELF entry remains `0x80200000`.

A repeated incremental build reports no work. A separate source/build directory
containing spaces is used to rebuild the finished overlay. Generated files and
compiled executables are not part of the delivery.

Local tools are Clang/LLD 17, GCC 14.2.0 and CMake/Ninja on Linux x86-64. The user's
Windows Clang 22 toolchain is not executed by this environment. No claim is made
that every diagnostic introduced by newer compilers was reproduced here.

## New native tests

The object-cache project has 65 registered cases: 64 policy/data tests and one
explicit guest-C-sequence host model. All 65 pass under each configuration:

* Clang Debug with AddressSanitizer and UndefinedBehaviorSanitizer.
* Clang Release with AddressSanitizer and UndefinedBehaviorSanitizer.
* GCC Debug with AddressSanitizer and UndefinedBehaviorSanitizer.

The actual cache, physical bitmap allocator, address checks and frame-query code
are compiled. Only the machine admission predicate is modelled in the policy
suite. A test-only symbol wrapper refuses selected frame releases without
changing the underlying bitmap; successful operations still call the real free.
The guest-sequence model additionally models serial output, CSR snapshots,
ECALL observations and balanced critical-section ownership.

The tests cover:

* Invalid geometry, null output, dirty or copied storage and repeated setup.
* Lazy acquisition, sharing, independent backing pages and every supported
  power-of-two alignment across eleven representative object sizes.
* Defined allocation bytes, scrubbing, exact-address/ticket/domain checks,
  stale reuse, pointer interior refusal and ticket exhaustion without wrap.
* Live-object close refusal, empty-page trimming, partial-release progress,
  closing-state admission refusal and successful cleanup retry.
* Tail corruption, disappeared/reserved backing frames, duplicate page records,
  invalid slot counts and metadata corruption with no unsafe force-free.
* 2,000 successive allocation lifetimes, 4,000 deterministic mixed operations,
  17 available-memory budgets and failure at each of four release positions.
* FREE/RESERVED/ALLOCATED frame observations and unchanged outputs on errors.
* The exact five-case guest C acceptance sequence under a labelled host model.

These are bounded tests, not exhaustive verification. Sanitizers see the host
arena and program memory; they do not automatically understand that each slot is
an independent allocation. The service's own tail/reference checks are tested
explicitly. Poisoned tests assert retained ownership and then end their isolated
host process; that is not a production recovery path.

## Existing native regression suites

| Suite | Result |
|---|---:|
| Process supervision | 54/54 |
| Blocking IPC | 49/49 |
| User scheduling | 53/53 |
| Message channels | 72/72 |
| Process registry | 49/49 |
| Executable loading | 68/68 |
| User memory | 24/24 |
| Trap integrity | 70/70 |
| Interrupt ownership | 68/68 |
| Cooperative threads | 55/55 |
| Events | 48/48 |

These are Clang Debug native runs with their existing sanitizer options.
Ordinary policy suites use ASan/UBSan. Alternate-stack thread and event targets,
and the interrupt suite's alternate-stack integration cases, use UBSan only;
the existing host context adapter does not issue ASan fibre notifications.

## Current-image fixture

The added `kernel.riscv64.object_caches` test requires `kernel_current_image`.
A separate validation copy is deliberately made uncompilable. CTest must fail
its build fixture and mark the dependent cache test Not Run, leaving its older
ELF unchanged. `/usr/bin/false` is used solely to allow test registration in
that host-side dependency experiment; it is never represented as QEMU or as a
passing guest. The corrected source is restored after the experiment.

## Guest execution boundary

RISC-V QEMU is not installed in this environment. No new RV64 guest execution
or native-machine hardware result is claimed. The Windows configure/build,
complete CTest run and manual QEMU invocation remain necessary before the
change is considered runtime-qualified on the selected target.

The new guest evidence is:

```text
object-caches-test=begin
object-cache.case=lazy-cache-and-frame-observation
object-cache.case=shared-slots-and-noncontiguous-frames
object-cache.noncontiguous-backing=pass
object-cache.case=checked-reference-and-stale-reuse
object-cache.stale-reference=refused
object-cache.case=explicit-trim-and-page-alignment
object-cache.case=unfinished-critical-section-refusal
object-cache.original-trap-handler=pass
object-cache.machine-state=unchanged
object-cache.frame-accounting=restored
object-cache.completed-cases=5
object-cache.completed-checks=<observed count>
object-caches-test=pass
UMICOM_KERNEL_OBJECT_CACHES_READY
UMICOM_KERNEL_END
```

The additional static cache metadata changes the linked Kernel end address and
its reserved-frame count. Final accounting is compared with the post-bootstrap
baseline, not with a hard-coded number from an older image.

## Remaining allocation work

This cache is not a general heap, demand-paged allocator, transferable physical
frame capability or mapped-region owner. Objects cannot span frames. Its guards
are diagnostic bytes, not inaccessible pages. Closing a cache does not cancel
borrowers, unwind callbacks or release a process's other resources.

The broader allocation/VM work package still needs explicit ownership of mapped
regions and protected stacks before it is marked complete. The VFS/RAM-backed
file work follows that foundation. Existing successful lower services remain
available while those new ownership boundaries are developed.
