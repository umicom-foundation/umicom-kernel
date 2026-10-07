# Umicom Kernel — Writable block validation and integration

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Verified baseline and delivery scope

The complete-file overlay is based on `main` commit:

```text
d8cdd9cbbe8457f7bc90e0daa0dc224ed1b38259
feat(kernel): mount checked read-only FAT16 through VFS
```

All twenty-eight files in the preceding filesystem-provider delivery were
compared byte for byte with that commit before implementation began. This
overlay therefore builds on the user's integrated filesystem-provider source.

The delivery adds explicit writable VirtIO block leases, one-to-eight-sector
WRITE, FLUSH, conservative mutation outcomes, native fault injection and two
dedicated real-QEMU qualification images. The companion implementation guide
explains the API and persistence boundaries. This remains transport work;
existing FAT16 mounts retain their read-only contract.

The ZIP contains seventeen complete files: eight existing files with insertions
and nine new files. It contains no patch fragments, Git metadata, build products,
new repository scripts or replacement disk fixtures. Existing source lines,
comments, author credits and licence headers remain intact and in order.
Superseded implementation remains in explained disabled blocks.

## Source-preservation results

| Existing file | Added lines | Removed lines |
|---|---:|---:|
| `CMakeLists.txt` | 4 | 0 |
| `docs/READ_ONLY_VIRTIO_BLOCK.md` | 5 | 0 |
| `include/umicom/kernel/virtio_block.h` | 66 | 0 |
| `include/umicom/kernel/virtio_block_protocol.h` | 5 | 0 |
| `kernel/block_console.c` | 7 | 0 |
| `kernel/main.c` | 6 | 0 |
| `kernel/virtio_block.c` | 230 | 0 |
| `tests/virtio_block/virtio_block_tests.c` | 9 | 0 |
| **Total** | **332** | **0** |

The independent audit reads each original file from its baseline Git object
and verifies every original line as an exact ordered subsequence of the
delivered file. `git diff --check` also passes.

Appending diagnostic fields enlarged `UmicomKernelBlockInfo`. The real RV64
link exposed a compiler-generated `memset` from an existing aggregate
initialiser. Narrow explicit byte clearing/copying now keeps those operations
freestanding; the original initialisers and assignment remain preserved.
Strict standalone driver compilation at O0 and O2 reports only the existing
physical-memory allocate/query/free dependencies, with no `memcpy`, `memset`
or other hosted runtime reference.

## Executed toolchain

| Component | Executed version or configuration |
|---|---|
| Host | Linux x86-64 |
| Clang and LLD | 18.1.3 |
| GCC native comparison | 13.3.0 |
| CMake | 4.4.4 |
| Ninja | 1.13.2.git.kitware.jobserver-pipe-1 |
| QEMU RISC-V | 8.2.2 |
| Kernel target | Existing freestanding RV64 target and integer `lp64` ABI |
| Language | C23 core and the existing Assembly startup/architecture code |

Both complete Debug and Release configurations build successfully, including
the two new dedicated images and all established configured images. The
existing firmware entry bridge and strict copied-DTB capture path remain in
use; no synthetic replacement DTB was injected for these guest runs.

The user's Windows toolchain was not executed in this environment. The
PowerShell commands below use the established project presets and are the
local integration gate. The optional native fault-injection project uses an
ELF host linker's wrapping support and is qualified on Linux.

## Native storage matrix

The actual driver, allocator, checked disk inspector, filesystem provider and
mount code pass the following matrix:

| Configuration | WRITE/FLUSH | Existing block | Disk inspection | Mounted filesystem | Total |
|---|---:|---:|---:|---:|---:|
| Clang Debug | 49/49 | 71/71 | 84/84 | 70/70 | 274/274 |
| Clang Release | 49/49 | 71/71 | 84/84 | 70/70 | 274/274 |
| GCC Debug | 49/49 | 71/71 | 84/84 | 70/70 | 274/274 |

That is 822 successful test executions across three configurations, with strict
warnings, AddressSanitizer and UndefinedBehaviorSanitizer enabled. Leak scanning
is disabled because the execution environment does not permit the required
process inspection. This is not a LeakSanitizer pass claim.

The new suite extends the existing MMIO model with two zero-default hooks; the
original read-only test behaviour remains the default. Visible and flushed
media are separate disposable arrays. The driver still performs the real
request building, DMA staging, completion checks, deadline checks, reset and
allocator ownership. The model does not stand in for executing RISC-V or QEMU.

The forty-nine named cases exercise:

- Explicit writable admission, read-only refusal, missing modern/FLUSH
  features, refused negotiation, a foreign active driver and retained failed
  admission cleanup.
- Zero, oversized, insufficient, overflowing and beyond-capacity requests;
  source/outcome overlap with the owner, each other and retained DMA storage;
  outcome alignment; unsafe context, reentry, exclusivity and stale handles.
- One-sector, final-sector and eight-sector transfers, non-contiguous owned
  pages, a staged input snapshot and caller storage changed after publication.
- Correct descriptor directions and status-only mutation completions; a
  device that consumes the available ring before the notification register.
- Partial media changes followed by IO_ERROR or UNSUPPORTED, sticky write
  uncertainty, explicit FLUSH after an error and failed FLUSH retaining dirty
  state.
- Missing, malformed, unwritten or unknown completions; changed configuration,
  device reset requests, elapsed timeouts and stopped/backward clocks.
- Immediate and delayed successful/error WRITE and FLUSH completions at the
  deadline, and clock regression that remains above the original timestamp
  but falls below the preceding observation.
- Late DMA completion after timeout, unacknowledged reset with unsanitised
  retained pages, allocation failure, partial release failure and cleanup retry.
- Cleanup without implicit FLUSH, explicit FLUSH on a clean lease, and 65,544
  mixed READ/WRITE/FLUSH requests crossing the full 16-bit ring-index wrap.

The test evidence is checked for complete executed cases, rather than treating
a truncated console stream as a finished run. Detailed CTest records and,
where generated, JUnit output are distinguished in the external review.

## Other native regression projects

All twenty-one remaining native projects pass once in Clang Debug against the
same source, for another 1,311 distinct cases:

| Project | Passed |
|---|---:|
| `blocking_ipc` | 49/49 |
| `boot_services` | 88/88 |
| `console_shell` | 65/65 |
| `events` | 48/48 |
| `executable` | 68/68 |
| `file_services` | 54/54 |
| `hardware_catalogue` | 103/103 |
| `interrupts` | 68/68 |
| `managed_services` | 77/77 |
| `mapped_regions` | 59/59 |
| `message_channels` | 72/72 |
| `object_cache` | 65/65 |
| `process_registry` | 49/49 |
| `process_supervision` | 54/54 |
| `program_launch` | 57/57 |
| `ramfs` | 66/66 |
| `standard_streams` | 67/67 |
| `threads` | 55/55 |
| `trap_integrity` | 70/70 |
| `user_memory` | 24/24 |
| `user_scheduling` | 53/53 |

Together with the four storage projects, the Clang Debug gate covers
**twenty-five projects and 1,585 distinct tests**. The additional Clang Release
and GCC runs repeat storage cases; they are not counted as new distinct tests.

Each project retains its established sanitiser configuration. The alternate
host-stack thread/event paths use UBSan, and the interrupt project retains its
existing mixed coverage; no unsupported ASan fibre-switch coverage is claimed.
The relevant loader/process tests receive an actual cross-built diagnostic ELF.

## Real QEMU qualification

The main CTest inventory is forty tests: the previous thirty-six and four new
entries for copy preparation, writer, fresh readback and cleanup. Complete
Debug and Release CTest runs each report **40/40 passed**, with no disabled or
skipped tests. Source hashes are captured around the guest runs and remain
unchanged.

The two new guest processes exercise actual modern VirtIO MMIO, DMA, RISC-V
barriers, device reset and the existing physical allocator. They attach the
generated raw copy with explicit writeback caching. The writer verifies all
128 original sectors, performs fifteen WRITE requests affecting twenty-two
sectors, reads back all changed and untouched bytes, completes FLUSH, closes,
rejects a stale handle, reopens and checks the complete disk again.

Successful writer output includes:

```text
block-write.original-fixture=fully-verified
block-write.bounds=refused-before-submission
block-write.single-page-final-sector-and-ring-reuse=verified
block-write.input-and-guards=unchanged
block-write.flush=completed
block-write.all-written-and-untouched-bytes=verified
block-write.reset-reopen-and-stale-lease=verified
block-write.frame-accounting=restored
block-write.machine-state=unchanged
UMICOM_KERNEL_WRITABLE_BLOCK_READY
UMICOM_KERNEL_END
```

The second QEMU process opens the same generated file read-only. Its successful
output includes:

```text
block-write-readback.mutation-authority=refused
block-write-readback.all-persisted-and-untouched-bytes=verified
block-write-readback.reset-reopen-and-stale-lease=verified
block-write-readback.frame-accounting=restored
block-write-readback.machine-state=unchanged
UMICOM_KERNEL_BLOCK_WRITE_READBACK_READY
UMICOM_KERNEL_END
```

This demonstrates the exercised transport, successful protocol FLUSH and bytes
surviving orderly QEMU process exit and reopening. It does not prove physical
power-cut durability, filesystem transactions, filesystem recovery or writable
FAT16 operation.

The filtered readback lifecycle passes five tests including its automatic
current-image, preparation, writer and cleanup prerequisites. A separate
failure reproduction verifies that an unsuccessful current-image build blocks
the guests while cleanup still removes an already-present disposable copy.
The final extracted-package check repeats that failure gate using the actual
delivered build, with its deliberately changed verification source restored
afterwards. Its results are recorded in the external HTML review.

One concurrent Release-run postcondition found a generated copy after CTest
had reported cleanup success. Filesystem tracing confirmed that cleanup really
deleted it, followed approximately 0.9 seconds later by an unexplained move
restoring a different inode with the written contents. The actor could not be
identified with the available process-inspection permissions. A separately
traced five-test lifecycle passed and ended with the copy absent. The remaining
copy was explicitly removed after all guests exited. No repository cleanup
change is made to compensate for an unidentified later restoration, and lasting
automatic removal in that concurrent run is not claimed. Both source fixtures
remained unchanged throughout.

## Immutable fixture hashes

The raw 128-sector source fixture remains:

```text
tests/virtio_block/fixture.raw
e5bea1290b3be59bdaf9f3a99d0be7527baefb3822b7d4b4e1c6c9736a4efa46
```

The existing FAT16 source fixture remains:

```text
tests/disk_inspection/fixture.raw
bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938
```

The generated writable disk is a separate disposable copy. The source fixtures
are not included again in this overlay because the verified baseline already
contains their exact bytes.

## Integrate and test from PowerShell

The ZIP is rooted at `umicom-kernel/`. Extract it into `C:\umicom` and merge
the included folder with the existing checkout. The command below assumes the
ZIP was downloaded to the normal Downloads folder:

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\Umicom_Kernel_Writable_Block_Leases_Write_Flush_and_Readback.zip" -DestinationPath "C:\umicom" -Force
Set-Location "C:\umicom\umicom-kernel"
git diff --check
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Stop at a failed command and inspect its first error. The full suite should
report forty tests when the configured QEMU executable is available. The new
writer and readback markers above must both appear in their test records.

To repeat only the complete writable-disk lifecycle through the readback
fixture's automatically selected prerequisites:

```powershell
ctest --preset riscv64-clang-debug -R "kernel\.riscv64\.block_write_readback$" --output-on-failure --no-tests=error
```

To verify the original disk source bytes locally:

```powershell
Get-FileHash -Algorithm SHA256 ".\tests\virtio_block\fixture.raw"
Get-FileHash -Algorithm SHA256 ".\tests\disk_inspection\fixture.raw"
```

After the build and tests pass, review and commit the complete files on `main`:

```powershell
Set-Location "C:\umicom\umicom-kernel"
git status --short
git add -A
git diff --cached --check
git diff --cached --stat
git commit -m "feat(kernel): add writable VirtIO block leases and flush"
git push origin main
```

No branch, commit or push was created while preparing this delivery. The
external review contains the final ZIP checksum, per-file manifest and the
fresh extraction/build verification results.

## Optional native suite on a supported Linux host

From the repository root with the required Clang/CMake/Ninja tools available:

```bash
cmake -S tests/virtio_block_write -B build/native-write-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DUMICOM_BLOCK_WRITE_SANITIZERS=ON
cmake --build build/native-write-tests --parallel 2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/native-write-tests --output-on-failure --no-tests=error
```

The explicit leak-scanning setting reproduces this execution environment's
restriction. The suite should execute all forty-nine cases. These model tests
complement the real guest lifecycle; neither substitutes for the other.
