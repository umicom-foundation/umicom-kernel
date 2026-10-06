# Umicom Kernel — Normal-startup validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Exact source baseline

The baseline is `ea9819d23164fc9c5931e1defbd45bd6b06e6ce5` in
`umicom-foundation/umicom-kernel`, containing structured arguments and launch
context. The build-relevant arch, cmake, include, kernel, platform, programs and
tests directory trees, root build files and licence match their Git blob/tree
identities. This does not claim a fresh audit of every historical document or
that committed source by itself proves guest execution.

| Existing file | Insertions | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| kernel/console_runtime.c | 11 | 0 |
| kernel/console_shell.c | 14 | 0 |
| kernel/main.c | 13 | 0 |
| Total | 42 | 0 |

Every original line remains unchanged and in its original order. No existing
implementation is superseded or disabled. Assembly, platform implementations,
allocators, VFS, scheduler, process supervisor, streams and launch packing remain
untouched. The original diagnostic/console images retain their cumulative path.
The new entry selects normal or forced recovery before that path starts.

## Toolchain and hardware boundary

Validation uses Linux x86-64, Clang/LLD 17, GCC 14.2, CMake 3.31.6 and Ninja 1.12.1.
The complete Kernel and independently linked programs cross-build for the existing
`riscv64-unknown-elf` target in Debug and Release. Strict warnings remain errors;
no new warning suppression, hosted runtime or scripting dependency is added.

The Windows compiler is newer than this cross-compiler. No Windows Clang pass is
claimed. RISC-V QEMU is not installed here: neither real normal boot, forced
recovery nor the new native job/timeout sequence has been executed locally.
Native tests model privileged entry and clock/register observations. Successful
compilation, a modelled deadline or an ELF header are not guest acceptance.

## Cross-build and packaged-source checks

Both Debug and Release build the diagnostic, interactive, normal-system, recovery
and isolated test images. All keep the original RV64 entry `0x80200000`. Repeating
the completed builds reports `ninja: no work to do`.

The complete source ZIP was extracted over a separately verified baseline copy
whose source and build paths contain spaces. All eight Kernel/image profiles
and seven separate program ELFs compiled and linked. The packaged native test
project is also rebuilt and rerun independently of the working source. All 88
cases pass. An incremental rebuild of the final packaged code performs no work.
The final archive contains only complete source/documentation files, not these
build products or the internal packaging/test driver.

The extra startup profiles are deliberately separate link targets, inheriting
established sources and feature definitions. Their initial compilation adds work;
it does not justify deleting the existing incremental build directory. The
system images can be disabled explicitly with UMICOM_BUILD_SYSTEM_IMAGES=OFF.

## New native acceptance

The new standalone project is `tests/boot_services`. Each configuration passed
**88/88 CTests**: Clang Debug, Clang Release and GCC Debug, with AddressSanitizer
and UndefinedBehaviorSanitizer. Tests halt on sanitizer errors.

There are 58 controller tests. They compile the real new controller, existing
supervisor, scheduler, launch packer, stream service, user dispatcher, IPC layer,
ELF loader, page walker and allocator. Hardware execution alone uses the existing
explicit model. The model's system calls still pass through the actual C trap
dispatcher and its origin/instruction/budget checks.

Coverage includes complete-plan preflight, disconnected cycles, copied inputs,
maximum eight-job admission, dependency order, optional/required failures,
backoff while independent work runs, exact retry boundaries, fresh retry
identities, output-before-collection, reentrant callbacks, malformed ELF,
architecture refusal, wall deadlines, execution budgets and 200 fresh manager
lifetimes. These counts describe bounded tests, not an exhaustive proof.

A test-only allocator-release wrapper checks both published-image collection
and unpublished loader rollback. Cleanup failures keep their handle and retry
position. Tests also delay cleanup beyond an already observed successful exit's
deadline: that delay must not rewrite the program outcome as a timeout.

The remaining 30 tests compile the actual normal-memory helper and independent
recovery interpreter. They run in separate host processes so the physical owner
really begins uninitialised. They check bad RAM arithmetic/alignment, Kernel
bounds, inaccessible/truncated DTB headers, overlap, exact reservations and
refused reinitialisation without losing existing allocations. The DTB is a
labelled synthetic fixed header, not a real hardware-discovery tree.

Recovery tests accept only the intended commands, reject extra tokens, prefixes,
case changes, controls, embedded NUL, malformed quotes and file/program verbs.
They confirm the allocator remains uninitialised after interpreting commands.
The recovery-support executable links no filesystem, process or Framework
implementation. These are interpreter tests, not an emulated UART session.

## Existing native regressions

All eighteen existing standalone projects passed after the additions:

| Project | Passed |
|---|---:|
| user memory | 24/24 |
| executable | 68/68 |
| process registry | 49/49 |
| message channels | 72/72 |
| user scheduling | 53/53 |
| blocking ipc | 49/49 |
| process supervision | 54/54 |
| object cache | 65/65 |
| mapped regions | 59/59 |
| ramfs | 66/66 |
| file services | 54/54 |
| console shell | 65/65 |
| standard streams | 67/67 |
| program launch | 57/57 |
| trap integrity | 70/70 |
| interrupts | 68/68 |
| threads | 55/55 |
| events | 48/48 |

The executable, registry and RAMFS suites also received the actual cross-built
launch-client ELF for their optional loader/file-source cases. That proves byte
parsing, ownership and copies, not execution of those instructions.

The ordinary native suites enable their existing ASan/UBSan options. Real
alternate-stack thread/event tests and the corresponding interrupt callback
tests use their existing UBSan-only configuration; no ASan fibre integration is
invented for those adapters.

## CTest registration and current-image guard

The ordinary RISC-V build registers 30 tests when QEMU is available: the previous
26 plus boot_services, startup-check, recovery-check and forced-recovery-check.
All new tests require the existing kernel_current_image fixture.

In that isolated packaged-source copy, a temporary compiler error was inserted
into the startup controller. CTest then failed kernel.build.current and marked
all four selected new runtime tests Not Run. The hashes of all fifteen already
linked ELF files remained unchanged. The temporary error was removed afterwards;
it is not present in either the working source or the final ZIP.

For local registration/fixture inspection only, an explicitly false executable
is supplied as the unavailable emulator. No successful runtime result is derived
from it. The failure check must stop at compilation and mark the dependent
runtime tests Not Run. This is separate evidence from an actual QEMU pass.

## Required Windows/QEMU qualification

Use the normal configure/build/CTest commands in NORMAL_STARTUP_AND_RECOVERY.md.
The cumulative boot-services check must execute six real cases, including actual
user output, dependency ordering, bounded retry and a CPU-bound timeout. The
normal-startup smoke must complete the two compiled launch jobs and restore its
frame accounting. The required-failure test must exhaust precisely its required
job's two attempts and never admit the dependent. Forced recovery must observe
an uninitialised physical owner.

Manually run umicom-system.elf, check the services report, exercise files and a
structured program launch, then poweroff. It must not print the cumulative
self-test sequence first. Manually run umicom-recovery.elf, inspect status,
confirm file/program commands are refused, and poweroff.

The automated startup smoke stops before opening the interactive shell. It is
not substituted for the manual terminal session. A recovery marker is accepted
by its dedicated negative test only when the exact expected selection is proven;
an unrelated boot error does not become a passing recovery test.

## Remaining work

This is one-shot boot orchestration. Long-running readiness/health protocols,
restart of persistent services, writable service configuration, a user-space
System Manager, persistent storage and authenticated recovery remain separate.
The system is still single-hart, RAM-only and polled. Source and copied launch
data are trusted Kernel inputs; no adversarial machine-mode isolation is claimed.
