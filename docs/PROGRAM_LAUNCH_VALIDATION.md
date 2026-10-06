# Umicom Kernel — Program-launch validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Source baseline

The delivery starts from `9f66fee037eba11b458bc6f234f703697b6e947d` in
`umicom-foundation/umicom-kernel`, containing task-owned standard streams and the
foreground terminal. The root build files and the build-relevant `arch`, `cmake`,
`include`, `kernel`, `platform`, `programs` and `tests` trees reconstructed for
validation match that commit's Git object identities. This does not claim that
an entire separate documentation/history checkout was reconstructed or tested.

| Existing file | Added lines | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| include/umicom/kernel/console_shell.h | 9 | 0 |
| include/umicom/kernel/process_supervisor.h | 5 | 0 |
| include/umicom/kernel/user_scheduler.h | 7 | 0 |
| kernel/console_line.c | 55 | 0 |
| kernel/console_runtime.c | 8 | 0 |
| kernel/console_shell.c | 61 | 0 |
| kernel/main.c | 7 | 0 |
| kernel/process_supervisor.c | 18 | 0 |
| kernel/user_scheduler.c | 26 | 0 |
| Total | 200 | 0 |

All original lines remain unchanged and in their original order. The single
superseded implementation is the 33-line body of the four-token parser. It is
retained verbatim under an explained disabled block; its public wrapper and the
structured commands share one active bounded tokenizer. There are no deletions,
renames, compatibility aliases or new scripts. Existing Assembly, platform,
allocators, VFS/RAMFS, IPC, stream implementation, user-copy implementation and
ELF parser are unchanged.

The accompanying review records each modified file's baseline Git blob, delivered
SHA-256, and complete existing-file difference. Thirteen new files provide the
launch implementation, new program, test project, CMake integration and guides.

## Local toolchain and what was not executed

The local validation host is Linux x86-64 with Clang/LLD 17.0.0, GCC 14.2.0,
CMake 3.31.6 and Ninja 1.12.1. Cross-builds target the established
`riscv64-unknown-elf` integer lp64 configuration. Clang 17 uses the C23 draft flag
selected by CMake. All existing strict warning/error options remain in force.

RISC-V QEMU is not installed in this environment. None of the guest acceptance
cases or actual RISC-V privilege transitions was executed locally. The owner's
Windows compiler was previously reported as Clang 22.1.8; no Windows Clang or
Windows terminal test is claimed. The standard Windows build, CTests and manual
interactive session remain required before committing the update.

## Complete cross-builds and packaging

Complete Debug and Release builds produce the normal Kernel, separate console
image, nested-fault image and independent launch-client ELF. Kernel entry remains
`0x80200000`; the launch-client entry is `0x00400000`. The new program has separate
RX text, read-only data and non-executable writable/BSS segments. It has no hosted
runtime or unexpected unresolved symbol requirement.

Repeating both builds reports `ninja: no work to do`. The finished source ZIP is
also extracted over a separate copy of the baseline and configured/built with
spaces in both its source and build paths. A repeated build in that copy is
incremental. The new native project is rebuilt and tested from that packaged
source rather than relying only on the construction workspace.

The generated Assembly carrier depends on the separate ELF file and its producer
target. Both the normal and independent nested-fault images receive the producer
dependency; the existing deferred console construction sees the same new sources
and definitions. No competing user-entry or context-switch Assembly is added.

## New native tests: 57 cases

All 57 tests pass in each of these configurations:

| Configuration | Instrumentation |
|---|---|
| Clang Debug | AddressSanitizer and UndefinedBehaviorSanitizer |
| Clang Release | AddressSanitizer and UndefinedBehaviorSanitizer |
| GCC Debug | AddressSanitizer and UndefinedBehaviorSanitizer |
| Packaged source, Clang Debug | AddressSanitizer and UndefinedBehaviorSanitizer |

CTest sets sanitizer errors to halt the test, rather than allowing a diagnostic
message followed by exit zero to be mistaken for success. The tests cover:

- Fixed layout, user-address relocation, array terminators and zero padding.
- Null spans, empty ordinary arguments, empty program names, embedded NUL,
  per-string/count/text limits and exact boundary capacities.
- Explicit empty environments, maximum entry count, case-sensitive names,
  duplicate names, name-prefix distinctions, invalid names and empty values.
- Misaligned, overflowing and non-canonical virtual block addresses.
- Fresh-task preparation, stale handles, unsafe/active contexts, wrong backing,
  invalid-plan no-change checks and copied input lifetime.
- Refusal to repeat preparation, overwrite structured entry with a numeric
  argument, or prepare a task which has already executed.
- One thousand successive task lifetimes with explicit destruction.
- Shared-tokenizer limits, quoted/empty arguments and late-quote refusal.
- Parent authority, sibling refusal, shell read-only/writable launch and syntax
  refusal without execution.
- The actual new program's C function: valid startup, legacy numeric refusal,
  a bad cookie and a missing pointer-array sentinel.

The suite compiles the actual C scheduler, supervisor, launch packer, command
engine, checked copies, streams, file services, IPC, loader, VFS/RAMFS, physical
allocator and mapper. It reuses the existing test-only execution/CSR model.
The direct program-C cases rebase a copied launch block into host memory and
replace only its stream-write primitive. They are not execution of RISC-V code.

## Existing native regressions

All seventeen existing native test projects pass against the updated source:

| Project | Passed cases | Instrumentation / integration boundary |
|---|---:|---|
| blocking_ipc | 49 | ASan + UBSan; privileged entry model |
| console_shell | 65 | ASan + UBSan; existing host input/execution models |
| events | 48 | UBSan; existing real alternate host-stack adapter |
| executable | 68 | ASan + UBSan; includes loading the actual new launch ELF |
| file_services | 54 | ASan + UBSan; privileged entry model |
| interrupts | 68 | 64 ASan + UBSan policy cases; 4 UBSan-only stack cases |
| mapped_regions | 59 | ASan + UBSan; architecture gate/publication model |
| message_channels | 72 | ASan + UBSan; privileged entry model |
| object_cache | 65 | ASan + UBSan; allocation gate/hardware model |
| process_registry | 49 | ASan + UBSan; includes the actual new ELF input |
| process_supervision | 54 | ASan + UBSan; privileged entry model |
| ramfs | 66 | ASan + UBSan; actual new ELF through file-to-loader integration |
| standard_streams | 67 | ASan + UBSan; privileged entry model |
| threads | 55 | UBSan; existing real alternate host-stack adapter |
| trap_integrity | 70 | ASan + UBSan; real C policy, modelled hardware |
| user_memory | 24 | ASan + UBSan; native page-backed memory tests |
| user_scheduling | 53 | ASan + UBSan; privileged entry model |

Alternate-stack suites do not claim AddressSanitizer support because their
existing adapter does not implement ASan fibre-switch notifications. Reading and
loading the actual separately compiled ELF verifies byte format and ownership;
it is not proof that those instructions run correctly in a guest.

## Current-image fixture failure check

In an isolated packaged-source build, a harmless failing executable path is
supplied only to register the QEMU tests. It is not used to manufacture a boot
result. An intentional compile error is then inserted into that disposable
source copy, and CTest selects `kernel.riscv64.program_launch`.

The automatically included `kernel.build.current` fixture fails to compile. The
dependent program-launch test is reported `Not Run`; the placeholder executable
is therefore never run. Hashes of the previously linked normal, console and
nested-fault ELFs remain unchanged. The deliberate source error is removed after
this check. Neither that error nor the placeholder setting is in the delivery.

This tests build/test dependency behaviour, not guest execution. CMake documents
that a failed fixture setup prevents its dependent test from running:
[FIXTURES_REQUIRED](https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html).

## Guest acceptance supplied, pending the owner's QEMU run

The new main test is `kernel.riscv64.program_launch`, with a thirty-second host
timeout and the existing `kernel_current_image` requirement. With QEMU available
at configure time, the main suite contains twenty-six tests.

The new guest sequence uses the actual console command engine, terminal output,
independent launch ELF and existing user-entry Assembly. It checks five cases:

| Case | Required observations |
|---|---|
| quoted-and-empty-arguments | argc=6, copied quoted/empty strings, explicit environment, both pointer-array sentinels, successful output and exit |
| maximum-argument-count | Sixteen arguments including program name; final element survives |
| syntax-refusal-without-execution | An unclosed final quote creates no foreground task or dispatch |
| legacy-numeric-entry | Existing diagnostic argument 7 returns assigned identity plus 40 |
| fresh-image-after-collection | New generation, new argument bytes and no earlier launch contents |

After task/terminal/filesystem cleanup, the guest compares machine controls,
parked timer compare and physical-frame accounting with their starting values.
Expected completion is:

```text
program-launch.argv-envp=verified-in-user-mode
program-launch.numeric-entry=preserved
program-launch.machine-state=restored
program-launch.frame-accounting=restored
program-launch.completed-cases=5
program-launch-test=pass
UMICOM_KERNEL_PROGRAM_LAUNCH_READY
```

These lines describe required guest evidence, not a local QEMU transcript. The
normal Kernel then reaches its existing final marker and finisher. The console
image continues to its normal prompt, where the user can run the quoted example
in PROGRAM_ARGUMENTS_AND_LAUNCH.md and verify it interactively.
