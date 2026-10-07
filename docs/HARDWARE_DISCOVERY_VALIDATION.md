# Umicom Kernel — Hardware-discovery validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Source identity and preservation

The baseline is `9d6ffa958f29b41cf3c542068e6f119020e03e32` of
`umicom-foundation/umicom-kernel`, containing managed-service readiness and health.
The live GitHub branch was read before preparation. The local build-relevant
`arch`, `cmake`, `include`, `kernel`, `platform`, `programs` and `tests` trees
were reconstructed from the supplied full-file deliveries and checked against
their exact Git tree identities at that commit. The root build files also match.
This is not a claim that every old documentation file was independently audited
or that every source path has executed on hardware.

| Existing file | Insertions | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| kernel/console_shell.c | 14 | 0 |
| kernel/main.c | 9 | 0 |
| kernel/normal_boot.c | 6 | 0 |
| Total | 33 | 0 |

Every original line remains in its original order. Nothing is superseded, so
there is no new disabled historical implementation. The original DTB header
inspector, platform memory geometry, UART/timer drivers, allocators, recovery,
service controller, process paths and all Assembly remain unchanged.

| Existing file | Baseline Git blob |
|---|---|
| `CMakeLists.txt` | `39d79f807be755b1bb27719297fc94c23e2e3016` |
| `kernel/console_shell.c` | `998995198316ee38996cad5887d5eee96cae754f` |
| `kernel/main.c` | `3f2cfa83756fc309dfa8da52e81bcaba51580f3c` |
| `kernel/normal_boot.c` | `acf752471d14fcb5e8add6ffb5446061d2f35e3b` |

Verified build-relevant directory identities:

```text
arch 9cca5e5d2e80521cf25734131bf137768bd39f7c
cmake 354743baf3891e4a20119aa48851db3b1d75404c
include 7075a99246e8afd6c35eaab6f6ae74c4bf7df991
kernel 65ce8aed430c10571ec7fc98221ae5cd92acfac7
platform da78c4738039898a916790c843f5eeb658a966a9
programs 38fa76c345a42fc8c78578909a9f00581b6928bb
tests 64e135ec768b0c9cb685199dc83cc3337b713884
```

## Toolchain and execution boundary

Validation used Linux x86-64, Clang/LLD 17.0.0, GCC 14.2.0, CMake 3.31.6 and
Ninja 1.12.1. Clang 17 uses CMake's supported C23 draft flag; the repository
continues to request C23. Warnings remain errors. No new warning suppression,
third-party DTB library or hosted runtime is linked into the Kernel.

RISC-V QEMU is unavailable in this environment. The new catalogue was not tested
against a live emulator-generated DTB, and the Windows Clang 22 build was not
run. Native fixtures are deliberately constructed protocol inputs, not disguised
real-hardware evidence. The user's guest run is required to qualify the actual
QEMU profile and the command's integration in the interactive shell.

## Cross-build and package checks

Complete Debug and Release builds compile and link the diagnostic, interactive,
normal/recovery and independent test images alongside all native program images.
The Kernel entry remains `0x80200000`. Repeated builds report no work to do.

The completed source ZIP is extracted over a separate baseline copy with spaces
in its source and build paths. That source configures, compiles and links, and
its separate native hardware suite passes 82/82 tests with sanitizers. Every
packaged file is compared byte-for-byte with the tested source. No build products,
internal packaging tools or test executables are included in the delivery ZIP.

The main configuration registers 32 CTests, including
`kernel.riscv64.hardware_discovery`, when a QEMU executable is configured. The
new test requires `kernel_current_image`. It runs the normal automatically
terminating diagnostic image, not an interactive prompt.

For configuration-only and negative fixture validation locally, the QEMU cache
entry points to `/usr/bin/false`. This is deliberately a non-emulator sentinel;
it is never counted as a guest pass. The real user's existing QEMU path is not
changed by the source delivery.

In an isolated packaged-source copy, a deliberate compile error is inserted
into the new reader. Selecting only the hardware test automatically includes
the build fixture. The fixture fails, the dependent hardware test is Not Run,
and all sixteen previously linked ELF files remain byte-for-byte unchanged.
The temporary error is then removed and that copy builds successfully again.
Neither the injected error nor the sentinel configuration is delivered.

## New native suite: 82/82 in three configurations

| Configuration | Instrumentation | Result |
|---|---|---|
| Clang Debug | AddressSanitizer + UndefinedBehaviorSanitizer | 82/82 |
| Clang Release | AddressSanitizer + UndefinedBehaviorSanitizer | 82/82 |
| GCC Debug | AddressSanitizer + UndefinedBehaviorSanitizer | 82/82 |

The suite compiles the actual new DTB reader, catalogue and boot/report adapter,
plus the unchanged minimum DTB inspector and address helpers. Its platform RAM
query is a host model which describes the fixture's real readable allocation.
There are no MMIO operations or modelled driver successes in these tests.

Coverage includes:

- Every truncated prefix of the representative fixture, each backed by an exact
  malloc-sized buffer for sanitizer bounds; unaligned input; header/block extent,
  overlap, format and alignment failures; reservation termination and overflow.
- Root/depth/token rules, property-before-child order, duplicate properties and
  siblings, exact paths, name limits, string-table suffix references, typed-value
  lengths and complete compatible lists.
- Non-inherited cell defaults; absent versus empty ranges; non-identity and nested
  translation; full-register-window containment; overlap and width overflow;
  visibly unsupported cell encodings instead of invented addresses.
- Forward phandle references, invalid or repeated handles, canonical/legacy
  disagreement, disabled ancestors, copied chosen aliases, timebase absence and
  malformed width, multiple RAM ranges and duplicate enabled hart IDs.
- All configured capacity boundaries, read-only input preservation, no usable
  partial catalogue after malformed input, storage-overlap refusal, retained
  copied values after the source bytes are overwritten, and one-shot capture.

One case makes 8,000 deterministic single-bit mutations of a constructed tree.
It opens each byte buffer, builds a catalogue only after successful structure
validation, and checks the published-state boundary. This is a regression corpus,
not exhaustive fuzzing or a correctness/security proof.

The report test clears the source blob before calling the actual report adapter.
It verifies copied device paths and addresses, the transport-slot warning, hart
classification and the explicit undecoded-IRQ wording. It does not execute the
new interactive shell command branch; that branch is cross-compiled, with the
real shell/guest execution left for the owner's QEMU check.

## Existing native regressions

All twenty existing projects were configured, built and tested against the
corrected full source with their supported sanitizer options enabled:

| Native project | Result |
|---|---:|
| `blocking_ipc` | **49/49 passed** |
| `boot_services` | **88/88 passed** |
| `console_shell` | **65/65 passed** |
| `events` | **48/48 passed** |
| `executable` | **68/68 passed** |
| `file_services` | **54/54 passed** |
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

Most suites use AddressSanitizer and UndefinedBehaviorSanitizer. Existing real
alternate-stack cases use UBSan only where their host context adapters lack
ASan fibre-switch notifications. No ASan coverage is claimed for those switches.
The unchanged ELF loader, registry and file-to-loader suites also receive the
actual separately cross-built health-service ELF as input; they do not execute
its RISC-V instruction stream.

Existing independent console tests keep their earlier compile-definition/link
surface. They verify regression of old behaviour, not execution of the new
hardware command. Full Kernel cross-builds include that command.

## Required guest acceptance

Keep the established QEMU options: `virt,aclint=off`, `-m 128M`, `-smp 1` and the
existing ELF-as-firmware boot contract. The new diagnostic check compares the
copied firmware description against that fixed qualification profile. It does
not infer support for another RAM size, hart count or physical board.

Expected successful ending:

```text
hardware-discovery-test=begin
hardware.catalogue-status=ok
hardware.fixed-profile-description=matched
hardware.virtio-transport-slots=<count>
hardware.mmio-probes=none
hardware.frame-accounting=unchanged
hardware-discovery-test=pass
UMICOM_KERNEL_HARDWARE_DISCOVERY_READY
UMICOM_KERNEL_END
```

A malformed/oversized description must give a diagnostic status rather than a
partially published inventory. The normal startup command can report the same
failure without claiming that fixed-profile drivers were reconfigured.

For the interactive check, run `bin/umicom-system.elf`, enter `hardware`, inspect
the device paths, translated/untranslated spans and warnings, then use the
existing `services` and `poweroff` commands. Check QEMU's exit code immediately.

Absolute node counts, optional-device counts and Kernel image addresses can
change with QEMU and source growth. A nonzero slot count describes advertised
transports only. No disk presence, device negotiation, IRQ route, DMA policy or
firmware reservation enforcement is part of this acceptance claim.
