# Umicom Kernel — Read-only block validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Baseline and source identity

This overlay is based on commit
`e883a1fe4e35df0ef07efee7d935132a3439676d` in
`umicom-foundation/umicom-kernel`, containing checked hardware discovery.

The build-relevant `arch`, `cmake`, `include`, `kernel`, `platform`, `programs`
and `tests` directory contents used for validation match their Git tree hashes
at that commit. The root CMakeLists.txt and CMakePresets.json also match their
Git blob hashes. This is not a claim that every historical document in the
working container constitutes a complete Git checkout.

Only three existing files are changed:

| File | Insertions | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| kernel/console_shell.c | 34 | 0 |
| kernel/main.c | 7 | 0 |
| Total | 45 | 0 |

Every original line remains unchanged and in its original order. Nothing is
superseded, so there is no new disabled-code block. Existing Assembly, hardware
capture, platform implementations, memory owners, schedulers, services,
filesystems and recovery code are not replaced. Fourteen additional files are
new, including these guides and synthetic fixture data; no script is added.

## Tools and what was not executed

Validation environment: Linux x86-64, Clang/LLD 17.0.0, GCC 14.2.0,
CMake 3.31.6 and Ninja 1.12.1. Cross compilation uses the existing
`riscv64-unknown-elf` toolchain and integer lp64 ABI with warnings as errors.
CMake selects Clang 17's supported C23 draft spelling. No warning suppression
or hosted runtime is added to the Kernel.

RISC-V QEMU is not installed in this environment. The real MMIO, device DMA,
RISC-V fence instructions, guest startup and interactive disk commands were
**not executed locally**. The user's Windows Clang 22 toolchain was also not
available. Windows compilation and guest qualification remain required.

A native protocol model supplies register-selector, status, reset, notification
and completion side effects. Passing that model is not proof that a particular
QEMU release or physical device accepts the sequence. It permits detailed
failure injection while keeping the actual driver C code under test.

## Cross-build and package checks

Complete Debug and Release builds succeeded, including all sixteen established
program and Kernel-profile ELFs. No independent block-program ELF is introduced;
the driver is a Kernel service and reads the raw fixture as data. The entry point
remains the existing `0x80200000`.

Repeated Debug and Release builds report `ninja: no work to do`. The final overlay
is also applied to a separate verified baseline with spaces in source and build
paths, followed by a complete Debug build and the new native suite. The exact
post-packaging results are recorded in the external preservation review.

The synthetic fixture is 65,536 bytes. CMake verifies its SHA-256 before copying
it into the build tree. The native fixture test independently checks every byte
against the shared deterministic format, including its human-readable label.
This does not represent reading the file through a guest device.

## New native suite

**71/71 tests pass** in each of Clang Debug, Clang Release and GCC Debug with
AddressSanitizer and UndefinedBehaviorSanitizer enabled. They compile the actual:

```text
kernel/virtio_block.c
kernel/block_platform_policy.c
kernel/block_console.c
kernel/virtio_block_validation.c
kernel/physical_memory.c
kernel/address.c
```

The native adapter replaces only platform/MMIO/timer observations and supplies
a bounded synthetic device. Linker wrapping injects frame allocation/release
failures while calling the original physical allocator for actual bitmap work.
The tests assert that a freed DMA page has been scrubbed and that the device
acknowledged reset before release. They verify the negotiated feature words,
fixed read request, descriptor directions, identity-probe stopping point and
permitted MMIO register accesses.

Coverage includes:

- Invalid initialisation, foreign active status, empty and legacy transports,
  malformed identity, missing mandatory features and writable-device refusal.
- Feature rejection, queue absence/size/activation failures, unstable, zero and
  64-bit capacity, and refusal of unsafe execution context or reentry.
- First/last sector, eight-sector reads, full-range preflight, caller-output
  overlap and unchanged output on refusal or unsuccessful completion.
- Invalid used IDs, lengths, index movement, unwritten/unknown status, unsolicited
  completion, changed configuration and device reset requests.
- Accepted IO_ERROR/UNSUPPORTED responses followed by successful retry.
- Elapsed timeout, stalled/backward clocks, delayed completion and delayed reset.
- Reset which never acknowledges: DMA frames and lease stay owned, including
  failed opening; subsequent successful reset permits explicit cleanup.
- Both frame-allocation refusal positions, five available-frame budgets, partial
  frame-release failures and retry, and non-contiguous queue/data allocations.
- Stale handles and generation exhaustion, 1,000 open/read/close lifetimes and
  65,544 reads crossing the complete 16-bit available/used index wrap.
- Qualified catalogue selection, sorting, disabled entries, wrong windows,
  duplicates, unknown translations, RAM/hart/clock restrictions and reserved RAM.
- Actual console report/read/retry functions and the exact guest C validation
  sequence, with explicit host replacements for hardware observations.

This is deterministic regression coverage, not an exhaustive model proof or a
malicious-device isolation test. The model shares the protocol layout constants
with the driver; an independent real-device run is still necessary to detect
any shared mistaken assumption.

## Existing native regression projects

All twenty-one existing native projects pass against the overlay:

| `blocking_ipc` | **49/49 passed** |
| `boot_services` | **88/88 passed** |
| `console_shell` | **65/65 passed** |
| `events` | **48/48 passed** |
| `executable` | **68/68 passed** |
| `file_services` | **54/54 passed** |
| `hardware_catalogue` | **82/82 passed** |
| `interrupts` | **68/68 passed** |
| `managed_services` | **77/77 passed** |
| `mapped_regions` | **59/59 passed** |
| `message_channels` | **72/72 passed** |
| `object_cache` | **65/65 passed** |
| `process_registry` | **49/49 passed** |
| `process_supervision` | **54/54 passed** |
| `program_launch` | **57/57 passed** |
| `ramfs` | **66/66 passed** |
| `standard_streams` | **67/67 passed** |
| `threads` | **55/55 passed** |
| `trap_integrity` | **70/70 passed** |
| `user_memory` | **24/24 passed** |
| `user_scheduling` | **53/53 passed** |

These runs use Clang Debug and each project's existing sanitiser option.
Most use AddressSanitizer plus UndefinedBehaviorSanitizer. The real alternate
host-stack thread/event paths use UndefinedBehaviorSanitizer only; the four
alternate-stack interrupt cases have the same restriction. Their adapter does
not implement ASan fibre-switch notifications. No ASan pass is claimed for
those paths. The loader, registry and RAMFS suites also receive the existing
cross-built diagnostic ELF as their real executable sample where supported.

## CTest registration and stale-image protection

There are thirty-four main tests when QEMU is configured: the thirty-two existing
tests plus `kernel.riscv64.block_without_device` and
`kernel.riscv64.read_only_block`. Both require `kernel_current_image` and have
bounded runtime timeouts.

The first test attaches no disk and requires the separate ABSENT marker. It
cannot earn the successful sector-read marker by doing no I/O. The second test
explicitly selects modern MMIO and attaches only the synthetic fixture with
`format=raw` and `readonly=on`.

A separate verification copy is used for a deliberately failing compilation.
A non-emulator stand-in is supplied solely to register and inspect CTest's
commands; it must not be reported as having run the guest. When the build fixture
fails, neither dependent block test may execute and the previously linked ELFs
must remain unchanged. The external review records the observed result.

## Guest acceptance still to be qualified on the user's machine

The no-device path must print:

```text
block.read-tests=not-run-no-modern-block-device
UMICOM_KERNEL_BLOCK_ABSENT_READY
```

The fixture-attached test checks actual returned byte values for the first and
last sectors, a complete eight-sector bounce-page transfer and repeated queue
slots. It refuses sector 128 of a 128-sector device, verifies unchanged output
on that refusal, closes, refuses the stale handle, reopens with a fresh token,
reads again and closes. Its successful end is:

```text
block.first-sector=verified
block.multi-sector-and-ring-reuse=verified
block.bounds-and-stale-handles=refused
block.reset-before-free=verified
block.machine-state=unchanged
block.frame-accounting=restored
block.disk-writes=none
read-only-block-test=pass
UMICOM_KERNEL_READ_ONLY_BLOCK_READY
```

The machine-state comparison covers the saved privilege/control fields used by
the existing supervisor observation structure. It does not claim that every
external interrupt-controller register is unchanged. No PLIC routing is added.

The diagnostic image deliberately requires exactly one known fixture when a
modern block device is present. It is not a general disk benchmark. Use the
normal `umicom-system.elf` plus the same fixture for manual `disks` and
`readsector` checks. No filesystem mount or persistence acceptance is implied.

After the manual read-only session, hash the fixture again. It must match:

```text
E5BEA1290B3BE59BDAF9F3A99D0BE7527BAEFB3822B7D4B4E1C6C9736A4EFA46
```

Use only the supplied disposable synthetic file. Keep `readonly=on` and the
modern-transport selection in the provided commands; never substitute a host
physical disk for this qualification.
