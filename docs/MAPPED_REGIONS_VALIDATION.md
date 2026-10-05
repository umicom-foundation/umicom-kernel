# Umicom Kernel mapped-region validation record

## Source basis and preservation

Base commit: `8742af36c3c651c36280da008781e3d9f06bb0ad` in
`umicom-foundation/umicom-kernel`.

The implementation starts from the committed object-cache/frame-observation
source, not from an earlier uncommitted correction. The compilation and native
test directories reconstructed from the supplied source archives were checked
against their live recursive Git tree identities:

| Directory | Git tree identity |
|---|---|
| arch | `18bb6883cfeb5cbfac165667e6f0cb091d5cc4bf` |
| cmake | `90cb8106b1b9d135ca45af0e2cbaaa296a8c3793` |
| include | `e933405e98d4e4a265a645895a44ad2fcb0fc71c` |
| kernel | `02ba630a16d80641f929478c701495854f9d20a7` |
| platform | `54fd3da5f5e7075dc8d38a04448215467a06cdfe` |
| programs | `6d388e806c051da4ae72848550d3e648f9b2c532` |
| tests | `4b856455035f06a0e9334a26c6cc1f653b807500` |

Four existing files receive insertions only: CMake integration, a dedicated
probe-text linker range, the main validation call and a checked mapping-rollback
release. Every previous source/comment line is retained unchanged and in order.
The formerly unchecked release statement remains disabled with an explanation.
There are no removed files, renamed files, new abbreviated aliases or scripts.
The delivery review provides the exact per-file counts and comparison.

## Tools actually used

* Linux x86-64 host.
* Clang 17.0.0 and LLD 17.0.0 from the available Swift LLVM toolchain.
* GCC 14.2.0 for the second native compiler configuration.
* CMake 3.31.6 and Ninja 1.12.1.
* LLVM objdump and ELF header/symbol inspection.

The host LLVM is not the Windows Clang 22 toolchain reported by the developer.
A clean cross-build here does not prove that a later compiler has no additional
warning. Strict warnings and `-Werror` remain enabled; none is suppressed.

## Performed cross-build checks

The complete normal Kernel, independent user programs and nested-trap diagnostic
image configure, compile and link in Debug and Release. The normal ELF retains
entry address `0x80200000`. Repeating the incremental build reports no work.

The complete-file overlay was also applied to a separate verified source copy
and built with spaces in both source and build paths. Rebuilding from the
finished delivery verifies the integration, not only standalone header syntax.

The new supervisor probe text is isolated into a page-aligned range, and all
of its entry, nested C helper, guard probes and completion/fault labels fall
inside that range. Disassembly confirms real stack stores and nested calls,
with no helper dependency into unmapped ordinary Kernel text or rodata.
These inspections do not execute the instructions.

## New native tests

The optional project is `tests/mapped_regions`.

| Configuration | Result |
|---|---|
| Clang Debug + AddressSanitizer/UndefinedBehaviorSanitizer | 59/59 passed |
| Clang Release + AddressSanitizer/UndefinedBehaviorSanitizer | 59/59 passed |
| GCC Debug + AddressSanitizer/UndefinedBehaviorSanitizer | 59/59 passed |

The real new owner, original page mapper/destructor, physical allocator and
address helpers are compiled. The architecture gate and instruction-publication
operation are host models. Two renamed physical allocator entry points permit
explicit, one-shot allocation/free refusals in this test project only.

Checks cover lazy planning, address arithmetic, canonical halves, guard
reservations, quotas, permissions, zero padding, source independence, scattered
backing pages, copies across page boundaries, complete-range refusal, readonly
writes, leases, stale returns, poisoned ownership, foreign table links, cycles,
wrong permissions, guard mappings, partial destruction and retry.

Repeated lifetimes exercise 1,000 isolated owners. Exhaustion tests use seventeen
available-memory budgets; separate allocation-refusal tests inject seventeen
positions. Teardown retry tests refuse each of ten releases in a representative
two-region layout. These are bounded tests, not proof over every possible input.

Corruption fixtures deliberately change otherwise private fields/PTEs. A
poisoned fixture checks refusal and retained accounting, then ends its host
process. That is not a supported way to repair or reset a live Kernel owner.

## Rollback defect: before and after

Using the new suite with the exact baseline `virtual_memory.c` gives 58 passes
and one failure out of 59. The combined-map-rollback-refusal case first refuses
a lower table allocation and then the parent's rollback release. The baseline
mapper disconnects that table despite the release refusal: one physical
allocation remains unreachable and the higher owner correctly detects an
accounting mismatch.

With the additive repair, the failed release leaves the parent linked/counted.
The existing destructor can then reclaim it. The combined-failure test returns
an explicit mapping error, shows zero outstanding allocations, retains the plan
and subsequently builds/closes it successfully. All 59 cases pass.

The repair does not suppress the backend failure or label it a successful
mapping. It preserves enough ownership to perform real cleanup.

## Existing native regressions

All suites were configured from the corrected source with their supplied
instrumentation options enabled. No production source was modified to make them
pass.

| Project | Result | Instrumentation |
|---|---|---|
| blocking_ipc | 49/49 | ASan + UBSan |
| events | 48/48 | UBSan; existing alternate-stack adapter |
| executable | 68/68 | ASan + UBSan; includes the actual linked diagnostic ELF |
| interrupts | 68/68 | Policy ASan + UBSan; alternate-stack subset UBSan |
| message_channels | 72/72 | ASan + UBSan |
| object_cache | 65/65 | ASan + UBSan |
| process_registry | 49/49 | ASan + UBSan; includes the actual linked diagnostic ELF |
| process_supervision | 54/54 | ASan + UBSan |
| threads | 55/55 | UBSan; existing alternate-stack adapter |
| trap_integrity | 70/70 | ASan + UBSan |
| user_memory | 24/24 | ASan + UBSan |
| user_scheduling | 53/53 | ASan + UBSan |

These host projects preserve their documented architecture substitutions. No
native result is described as RISC-V privilege-transition evidence. The existing
alternate-stack adapters lack ASan fibre notifications, so their tests are not
claimed as ASan-instrumented stack-switch validation.

## CTest registration and stale-image negative test

With a deliberately labelled host sentinel in the emulator cache variable,
CTest registers twenty-one main tests, including `kernel.riscv64.mapped_regions`.
The new test requires `kernel_current_image`, the established build fixture.

In an isolated copy, a real compile error was introduced after producing the
Kernel ELF. CTest reported the build fixture failed and the dependent mapped
region test Not Run. The original ELF's SHA-256 stayed unchanged and the
sentinel recorded no invocation. The sentinel is NOT an emulator and never
produces passing guest evidence. It is not included in the source delivery.

## Not performed in the delivery environment

RISC-V QEMU is not available here. No new real guest result is claimed.
Windows Clang 22, physical boards, SMP, arbitrary nested traps and concurrent
page-table modification were not validated.

The developer's QEMU run must still prove the exact causes, PCs, previous
privilege, stack-overflow address, restored machine controls and final frame
accounting described in `MAPPED_REGIONS_AND_PROTECTED_STACKS.md`.

## Acceptance and remaining scope

After a successful Windows build, run the existing CTest preset and then the
normal manual QEMU command. Require `UMICOM_KERNEL_MAPPED_REGIONS_READY`, the
ordinary Kernel end marker and exit code zero before committing.

This service owns new private layouts. It does not retrofit every existing root,
replace machine stacks, make Bare M-mode accesses subject to guard pages, provide
an unrestricted virtual heap or complete system-wide frame-generation tracking.
The next planned work is VFS and RAM-backed files with separately reviewed
metadata, buffer and descriptor ownership.
