# Umicom Kernel — Disk-inspection validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Exact baseline and preservation

This delivery starts from `50e532b4f1b784366c9f4dd06335231bc10a397e` in
`umicom-foundation/umicom-kernel`, containing the read-only VirtIO block driver.
The validation copy's build-relevant `arch`, `cmake`, `include`, `kernel`,
`platform`, `programs` and `tests` trees were checked against their exact Git
identities at that commit. The root build files also match. This is not a claim
that every historical document or local developer file was audited.

| Previously tracked file | Inserted lines | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| kernel/block_console.c | 5 | 0 |
| kernel/console_shell.c | 20 | 0 |
| kernel/main.c | 7 | 0 |
| Total | 36 | 0 |

Every original line remains unchanged and in its original order. The only
superseded material is the block-console output sentence which predates
partition/filesystem inspection. It remains under an explained `#if 0` block;
the active replacement describes the raw transport instead of claiming that
the whole Kernel has no partition/filesystem support. No algorithm is disabled.

The driver, hardware catalogue, platform adapters, allocators, VFS, RAMFS,
process paths and all Assembly are unchanged. The package has four complete
replacement files and seventeen new files, including two guides and one raw
fixture. There are no scripts or compiled binaries in the source delivery.
The optional fixture generator is native C source, not a general formatter.

## Tools and execution boundary

Validation was performed on Linux x86-64 with Clang/LLD 17.0.0, GCC 14.2.0,
CMake 3.31.6 and Ninja 1.12.1. CMake selects the available C23/draft spelling for
the compiler. The target remains `riscv64-unknown-elf`, RV64 integer lp64, with
strict warnings treated as errors. No warning is suppressed by this change.

RISC-V QEMU is not installed in this environment. The real guest MMIO, DMA,
reset and filesystem-reading sequence was **not executed here**. No Windows
Clang 22 result is claimed. The owner's Windows build and supplied QEMU test
remain required qualification, not a formality replaced by the native tests.

## Cross-build and package checks

The complete Debug and Release builds compile and link the normal diagnostic,
interactive, normal-startup, recovery and existing specialised test images,
plus the new `tests/umicom-disk-inspection.elf`. The new image entry remains
`0x80200000`. It uses the same established platform and essential boot setup.

Repeated builds report `ninja: no work to do`. A separately reconstructed
baseline with spaces in both source and build paths was overlaid from the ZIP,
built successfully and then rebuilt incrementally. The native tests were also
built and executed from that overlaid source. These checks exercise archive
layout, generated paths and the actual complete replacement files.

The new CMake fragment checks the fixture's hash before copying it. A comma in
the fixture build path is refused because QEMU uses commas to delimit `-drive`
options. The ordinary raw-sector fixture and its acceptance test remain intact.

## Native core tests — 72 cases

The core suite compiles the actual primary-partition parser and FAT16 inspector.
It reads a mutable host copy of the complete synthetic disk through the same
single-sector callback contract used by the device adapter. Test-only callbacks
record bounds, count reads and inject failures; they are not a second parser.

The cases cover primary-table signatures, sparse slots, empty-record
consistency, flag values, overlap, adjacency and wide arithmetic; explicit GPT
and extended-table refusal; all admitted BPB geometry; FAT12/FAT32 refusal;
clean flags and mirrored-table disagreement; owner lifetime; root and nested
short aliases; malformed path grammar; deleted/long-name records; directory
and file-chain bounds; cycles; EOF; output preservation and I/O failures.

The fixture uses one-sector clusters, and a separate accepted geometry test
uses two-sector clusters. File reads start at unaligned byte offsets and cross
non-contiguous cluster links. Slack bytes are deliberately different from file
contents. A mismatch in a later consulted FAT sector is tested, not only a bad
first sector at Open.

Additional loops exercise 1,000 successive owner lifetimes and 4,000
deterministic single-bit mutations in the MBR/volume headers. Every callback
is bounded by the configured disk extent. Every read position in a
representative valid file operation is failed in turn; unsuccessful reads must
leave both the caller's complete output buffer and its output count unchanged.
These bounded cases are regression evidence, not an exhaustive proof or a
full-volume fuzzer.

## Native transport and command tests — 12 cases

The transport suite compiles the actual new parser, inspector, console adapter
and guest C acceptance routine with the existing VirtIO block driver,
qualification policy and physical allocator.

It reuses the previous block suite's unchanged MMIO/DMA model. Device register
side effects, queue completion, timer observations and platform context are
modelled. The fixture byte source is changed to the new synthetic FAT16 image.
The production driver and completion checks are not stubbed out.

Cases cover fragmented data through the real driver; partition, geometry,
directory and file commands; binary-output escaping; malformed media;
malformed used-ring completion; retained reset/close retry; I/O refusal without
output changes; and parsing the actual console command forms. One case executes
the exact new guest C validation routine, including allocator and control-state
comparisons, against modelled hardware.

Calling the guest C routine in a host model does not establish a QEMU pass.
It checks its assertions and its wiring to the actual C services.

## New-suite results

| Configuration | Instrumentation | Result |
|---|---|---:|
| Clang Debug | AddressSanitizer + UndefinedBehaviorSanitizer | 84/84 |
| Clang Release | AddressSanitizer + UndefinedBehaviorSanitizer | 84/84 |
| GCC Debug | AddressSanitizer + UndefinedBehaviorSanitizer | 84/84 |
| Overlaid package, Clang Debug | AddressSanitizer + UndefinedBehaviorSanitizer | 84/84 |

## Existing native regressions

All twenty-two existing native projects were rebuilt against the overlaid
source with their sanitizer options enabled. The suites which switch real
alternate host stacks retain their documented UBSan-only instrumentation;
no unsupported ASan fibre-switch qualification is implied. The interrupt suite
likewise keeps the instrumentation split already specified in its build file.

| Existing project | Passed |
|---|---:|
| blocking_ipc | 49/49 |
| boot_services | 88/88 |
| console_shell | 65/65 |
| events | 48/48 |
| executable | 68/68 |
| file_services | 54/54 |
| hardware_catalogue | 82/82 |
| interrupts | 68/68 |
| managed_services | 77/77 |
| mapped_regions | 59/59 |
| message_channels | 72/72 |
| object_cache | 65/65 |
| process_registry | 49/49 |
| process_supervision | 54/54 |
| program_launch | 57/57 |
| ramfs | 66/66 |
| standard_streams | 67/67 |
| threads | 55/55 |
| trap_integrity | 70/70 |
| user_memory | 24/24 |
| user_scheduling | 53/53 |
| virtio_block | 71/71 |

The optional executable, registry and VFS byte-bridge cases receive the actual
cross-built `umicom-diagnostic.elf`. These tests inspect/load those bytes but
do not execute their RISC-V instructions on the host.

## Current-image fixture failure

An isolated packaged-source copy was successfully built first. A deliberate
`#error` was then added only to that disposable copy's new partition source.
Running the new disk-inspection CTest selected its existing build fixture.

Observed results:

- `kernel.build.current` failed compilation.
- `kernel.riscv64.disk_inspection` was **Not Run** due to the failed dependency.
- All **17** previously linked ELF file hashes were unchanged.
- Restoring the source allowed the build to succeed again; the following
  incremental build reported no work to do.

Because QEMU is unavailable, registration used `/usr/bin/false` as a
non-emulating stand-in solely for this dependency check. The dependent command
was not executed. This is evidence of build-fixture behaviour, not device or
guest execution. The generated test graph contains **35** main tests, with the
new test using its dedicated image and dedicated read-only fixture.

## Fixture reproducibility

The optional CMake target `umicom-disk-fixture` was built natively. It recreated
an independent 8,388,608-byte file which was byte-identical to the packaged raw
image. Repeating the operation against that existing path returned failure and
left its hash unchanged.

Recorded SHA-256:

```text
BD866D6AE337527F3A8B4609F31969D60185E025F8525726F573D905A2F50938
```

No image bytes were imported from a physical disk. Reproducibility of this
fixture is not a test of compatibility with every external FAT formatter.

## Required guest acceptance

`kernel.riscv64.disk_inspection` attaches this fixture read-only to modern
VirtIO MMIO and runs the new dedicated image. It must verify the MBR partition,
FAT geometry, root/nested names, exact text-file contents, fragmented bytes,
unaligned reads, EOF, invalid-path output preservation, transport reset and
restored physical accounting/control state.

The required marker is:

```text
UMICOM_KERNEL_DISK_INSPECTION_READY
```

An absent device cannot print that success marker. Existing absence/raw-block
tests keep their own markers and old fixture. For interactive qualification,
use `umicom-system.elf` with the new FAT16 fixture as documented in the guide.
Do not attach it to the cumulative diagnostic image and expect the old
128-sector raw-pattern check to accept unrelated bytes.

## Native commands for contributors

These optional Linux host commands do not replace Windows/QEMU qualification:

```sh
cmake -S tests/disk_inspection -B build/native-disk-inspection -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug \
    -DUMICOM_DISK_SANITIZERS=ON
cmake --build build/native-disk-inspection --parallel 2
ctest --test-dir build/native-disk-inspection --output-on-failure --no-tests=error
```

The transport suite currently uses the existing GNU/LLD `--wrap` host test
mechanism and Linux model assumptions. No Windows-native test-suite portability
is asserted merely because the freestanding Windows cross-build is supported.

## Remaining qualification

No real emulator, device, controller cache or physical medium was exercised
locally. The parser requires an unchanged medium from Open to Close. It does
not authenticate content, audit global cross-links, repair dirty media, mount a
VFS provider, provide user file handles or issue disk writes. Any later write
path needs its own ordering, reset, failure and recovery qualification.
