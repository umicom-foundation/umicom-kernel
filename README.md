# Umicom Kernel

**Umicom Kernel** is the original open-source kernel research and native-kernel implementation project for **Umicom OS**.

Project lead and author: Sammy Hegab  
Organisation: Umicom Foundation  
Primary implementation language: C23, with architecture-specific Assembly where justified  
Licence: MIT, unless a file or imported third-party component states otherwise

## Purpose

Umicom OS is a kernel-independent operating-system platform.

The current production kernel for the free-system distribution track is GNU Linux-libre. FreeBSD is a portability and server/appliance evaluation target. Umicom Kernel is the original native kernel implementation under development.

The long-term objective is that Umicom applications and Umicom Framework services can run unchanged above a stable Umicom System Services boundary while the kernel implementation underneath can be Linux-libre, FreeBSD or Umicom Kernel.

## Repository boundary

This repository owns only original kernel research and native-kernel implementation:

- architecture bring-up;
- boot and early machine initialisation;
- virtual/physical memory;
- interrupts and exceptions;
- scheduler, threads and processes;
- kernel object and handle model;
- capabilities and privilege boundaries;
- IPC, channels and shared memory;
- timers and clocks;
- kernel ABI/syscall contracts;
- VFS foundations;
- device-driver framework and selected drivers;
- kernel-side security mechanisms;
- kernel test harnesses, fuzzing and promotion evidence.

It does **not** own:

- Umicom Framework;
- Umicom applications;
- the complete installable Umicom OS distribution;
- graphical desktop components;
- package repository policy;
- full recovery/distribution image composition.

Those remain in their existing repositories.

## Dependency rule

```text
Umicom applications
        |
        v
Umicom Framework
        |
        v
Umicom System Services API
        |
        +----------------------+----------------------+
        |                      |                      |
        v                      v                      v
Linux-libre adapter       FreeBSD adapter       Umicom native adapter
                                                       |
                                                       v
                                                 Umicom Kernel
```

The kernel must never depend on the full Umicom Framework.

## Initial architecture target

The first execution target is QEMU RISC-V 64 `virt`.

The second architecture target is x86-64 with a documented firmware/boot route.

ARM64 follows after the kernel model and driver interfaces are stable enough that another architecture tests the abstraction rather than multiplying unfinished ports.

## Design direction

Umicom Kernel is planned as a small hybrid-microkernel architecture:

- keep privileged kernel mechanisms small;
- use explicit object handles and rights;
- make IPC/channels first-class;
- move restartable policy/services to user space where practical;
- keep device/server boundaries reviewable;
- provide a small native ABI;
- provide POSIX compatibility above the native ABI so existing free software can be ported without dictating the native design.

## Originality rule

The project studies operating-system concepts and public documentation from GNU/Linux, Unix, FreeBSD, Windows NT, illumos/Solaris, QNX, seL4, Fuchsia/Zircon and other systems.

"Inspiration" means learning from documented concepts, interfaces, failure modes and engineering experience. Umicom Kernel source should be original unless a third-party component is deliberately imported under a compatible licence and recorded with exact provenance.

Do not copy source simply to reproduce a feature.

## Capability navigation

Read [Find your way through Umicom Kernel](docs/CURRENT_CAPABILITIES.html) for the current source-level capability map and its limits. It distinguishes native console and storage foundations from a complete graphical operating system.

## Current implemented foundations

The active RISC-V research profile currently demonstrates:

- freestanding machine-mode boot into C23;
- early serial diagnostics;
- synchronous exception handling and machine-timer interrupts;
- physical page-frame ownership and reservation;
- construction, validation and software translation of Sv39 4 KiB page tables.

Hardware translation now has dedicated RISC-V activation and supervisor/user execution paths. See [Hardware address translation](docs/HARDWARE_ADDRESS_TRANSLATION.md) for their contracts and qualification.

<!-- The page-table-only description below predates hardware activation. The
hardware translation guide replaces it; the original wording remains here for
engineering review rather than being discarded. -->
<!--
Address translation is not enabled in hardware yet; the page-table subsystem is
being validated before supervisor/user execution activates virtual addressing.
-->

Educational guides are organised by capability rather than development batch:

- `docs/FIRST_BOOT.md`;
- `docs/TRAPS_AND_TIMER.md`;
- `docs/PHYSICAL_MEMORY.md`;
- `docs/VIRTUAL_MEMORY.md`;
- `docs/BUILD_AND_TEST.md`.

## Source naming and comments

New Kernel APIs use the full `Umicom...` name.  Temporary batch numbers, build
labels and chronology do not belong in API names or implementation comments.
Git history records when work was introduced; source comments explain behaviour,
ownership, architecture and failure rules so the code remains useful to future
developers.

## Current production relationship

Umicom Kernel is not silently selected by an Umicom OS release profile.

Until the promotion gates are satisfied:

```text
Production Umicom OS -> GNU Linux-libre
Research profile      -> Umicom Kernel
Portability proof     -> FreeBSD
```

## Source-preservation rule

Do not delete existing code, comments, experiments or superseded implementations merely because a design changes. Preserve historical implementations for review unless the project owner explicitly approves physical deletion.

## Development policy

- commit directly to `main`;
- no feature branches unless the project owner changes this rule;
- C23 first;
- Assembly only where architecture bring-up, context switching, atomic/interrupt entry or measured low-level work requires it;
- keep public ABI contracts small and versioned;
- no hidden fallback that turns an unexecuted test into success;
- QEMU evidence is separate from host unit-test evidence;
- physical-hardware support is never inferred from QEMU success.

Start with `docs/ARCHITECTURE.md`, `docs/DECISION_REGISTER.md`, `docs/ROADMAP.md` and `docs/PROMOTION_GATES.md`.

## Checked updates to existing FAT16 file data

The explicit trusted-console updater can change a bounded range within an
existing file allocation and flush its separate writable lease. Whole-volume
allocation checks precede mutation, and partial or uncertain writes retain
their evidence. See [Existing FAT16 data updates](docs/FAT16_DATA_UPDATES.md)
and [FAT16 update validation](docs/FAT16_UPDATE_VALIDATION.md) for the supported
profile, failure semantics, disposable-disk tests and integration commands.

## Ordered FAT16 commits and persistent interruption detection

The separate `fatcommitopen`, `fatstage` and `fatcommit` console path establishes
durable dirty FAT guards before updating file data, verifies complete sectors,
and explicitly finishes clean finalisation. An unfinished Stage remains dirty
across resource-only Close and is refused by a fresh ordinary reader. Results
retain partial writes and final-acknowledgement uncertainty. See
[Ordered FAT16 commits](docs/FAT16_ORDERED_COMMITS.md) and
[Commit validation and integration](docs/FAT16_COMMIT_VALIDATION.md).

<!-- Original ordered-commit scope, retained from before the lifecycle and VFS
extensions described below:
This remains a bounded existing-file data utility. Timestamp/ordinary directory
metadata updates, allocation changes, general writable VFS access and recovery
remain further work towards the persistent-system release gate.
-->
The `fatcommit` utility remains a bounded existing-file data interface. The
lifecycle and writable VFS interfaces below add metadata, allocation and native
file-service access using the same persistence engine. Recovery remains further
work towards the persistent-system release gate.

## Persistent FAT16 regular-file lifecycle

The dedicated file lifecycle owner can create regular files, append with new
allocation, truncate and delete, then reuse freed clusters. Each operation
requires an explicit Finish, and several accepted operations can share one
exclusive live session. The trusted console exposes `fatfsopen`, `fatcreate`,
`fatappend`, `fattruncate`, `fatdelete` and `fatfscommit`. See
[Persistent FAT16 file lifecycle](docs/FAT16_FILE_LIFECYCLE.md) for exact
metadata and allocation rules, bounds, interruption semantics, reboot tests
and disposable-disk instructions. General writable VFS access and recovery
remain outside this interface.

## Persistent FAT16 directory lifecycle

The same exclusive lifecycle owner now creates nested directories with explicit
calendar fields, removes empty directories, and reuses their allocation.
When an allocated parent runs out of entry slots, creation can extend its chain
within the documented limits. The fixed FAT16 root cannot grow.
See [Creating and removing FAT16 directories](docs/FAT16_DIRECTORY_LIFECYCLE.md)
for `fatmkdir`, `fatrmdir`, publication ordering and refusal rules.

For same-parent rename and cross-parent moves, see [Moving FAT16 files and directories](docs/FAT16_FILE_MOVES.md). The lifecycle session preserves data and calendars and requires an explicit Finish for every move.

## Writable FAT16 VFS and native process file services

An explicitly selected writable mount now connects the persistent lifecycle
engine to the existing VFS and copied native file-service requests. It supports
bounded positional writes and EOF growth, append, zero-extension, truncation,
file/directory creation and removal of closed objects. Each successful mutation
includes an accepted clean Finish before returning to the caller. Node identities,
descriptor positions, rights, namespace iteration and retained failure cleanup
follow the documented mutable-provider contract.

See [Writable FAT16 through native file services](docs/WRITABLE_FAT16_VFS.md) for
the APIs, `mountdiskrw` console workflow, same-lease reads, independent whole-disk
tests and fresh-boot validation. The older read-only mount and manually staged
console paths remain available. Automatic persistent program routing, an
installer and recovery remain separate roadmap work; this native Kernel code
does not depend on Umicom Framework.

Use [Testing the writable disk console](docs/WRITABLE_CONSOLE_TESTING.md) for
the PowerShell runner that creates a test disk, drives the normal console and
verifies a file after reboot. The guide also explains mount argument errors,
RAM and disk command namespaces, and host-terminal paste.
