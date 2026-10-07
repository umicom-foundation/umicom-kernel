# FAT16 update validation and integration

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Verified remote baseline

This delivery was developed from remote `main` at:

```text
Repository: https://github.com/umicom-foundation/umicom-kernel
Commit:     ffe27d81672b4836ef67a24ca2a62ca7a5bb742d
Subject:    feat(kernel): add writable VirtIO block leases and flush
```

The remote ref was checked with Git and the connected GitHub repository/commit
interface. A fresh checkout was clean. Every one of the 17 files in the previous
writable-block delivery matched that commit byte for byte. This is an increment
on the accepted transport work, not a reconstruction from an assumed roadmap.

The source remains on `main`. The full-file archive contains only changed and
new repository files under one `umicom-kernel/` directory. It contains no build
products, physical-device paths, replacement source fixtures or unrelated
Framework material. Commit and push remain explicit integration steps below.

## Source preservation and implementation review

Every original line of each modified tracked file remains byte-identical and in
its original order. The audit includes comment, author, licence, whitespace and
newline bytes. Superseded enum/banner text is retained in explained disabled
blocks; the old `about` text remains the active fallback for console builds
without the new updater. Original parser functions and existing source fixtures
are unchanged in behaviour.

New source names describe their responsibilities and contain no delivery-number
suffixes. The C23 Kernel implementation uses fixed owner storage, bounded loops,
volatile byte copies/clears and the existing Assembly/platform entry path.
Independent review covered allocation crosslinks, orphan predecessors, strict
directory records, copied owners, reentry, request timing, mutation evidence and
reset-before-release cleanup.

Review found and corrected two concrete issues before the final qualification:

- Each path byte must be checked for overlap with input/result storage before
  reading it. Otherwise an invalid path into a non-terminated result could scan
  beyond the result before eventual rejection. Both planner and updater now
  perform that early check, with native regression coverage.
- A CTest success-marker property alone can ignore an ordinary nonzero process
  exit. The new QEMU tests therefore use the same CMake module in script mode
  to require a zero QEMU exit and exact ordered READY/END lines. No additional
  repository runner script is introduced.

No claim of a transactional write, complete writable FAT driver or physical
power-loss safety follows from this work. See `FAT16_DATA_UPDATES.md` for the
scope and the persistent metadata work that remains.

## Executed qualification

| Qualification | Observed result |
| --- | --- |
| Complete RV64 Debug CTest suite | 44/44 passed, no skipped tests |
| Complete RV64 Release CTest suite | 44/44 passed, no skipped tests |
| New native suite, Clang Debug with ASan/UBSan | 163/163 passed |
| New native suite, GCC Release without sanitizers | 163/163 passed |
| Five established native regression projects | 339/339 distinct cases passed |
| Filtered process-gated FAT16 writer/readback lifecycle | 5/5 passed |
| Independent QEMU process-gate fault cases | 8/8 accepted/refused as expected |
| Real normal-system console scenarios | Both explicit-close and active-lease poweroff passed |
| Independent host C comparison of final guest-written disk | All 8,388,608 bytes checked; exactly 700 intended changes |

The five established native projects contain 339 distinct cases:

| Existing project | Cases | Configuration |
| --- | --- | --- |
| `disk_inspection` | 84 | Clang Debug, ASan and UBSan |
| `disk_filesystem` | 70 | Clang Debug, ASan and UBSan |
| `virtio_block` | 71 | Clang Debug, ASan and UBSan |
| `virtio_block_write` | 49 | Clang Debug, ASan and UBSan |
| `console_shell` | 65 | Clang Debug, ASan and UBSan |

The console project was rebuilt and its 65 cases repeated after the final
conditional `about` text change. The other four established source paths did
not change after their completed regression runs. The broader guest suites
exercise all registered images; this report does not present earlier native
results from unrelated projects as new executions.

Each native test runs the production parser/updater/transport code. The model
implements the MMIO/DMA boundary and keeps independent initial, visible, durable
and expected whole-disk images. The expected file mapping does not use the
planner's returned sector plan. The optional host image verifier compares the
actual guest-written copy against the original fixture independently of the
guest's expected-sector generator.

The observed toolchain was Clang/LLD 18.1.3, GCC 13.3.0, CMake 4.4.4,
Ninja 1.13.2.git.kitware.jobserver-pipe-1 and QEMU RISC-V64 8.2.2. The
163 new cases were run once per final compiler configuration, giving 326
successful executions and 163 distinct cases. Together with the 339 existing
regressions, this delivery directly qualified 502 distinct native cases. The
GCC Release run intentionally had sanitizers disabled; it complements the
Clang Debug ASan/UBSan run.

Address and undefined-behaviour checking were enabled as reported. Leak scanning
was disabled because this execution environment does not permit the required
process inspection; no leak-scan pass is claimed. Native qualification used:

```text
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
LSAN_OPTIONS=detect_leaks=0
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

Inventories were captured before tests. JUnit case lists, explicit subprocess
completion records and stable copies of detailed CTest logs were checked for
the reported executions; truncated terminal output was not used as a substitute
for completion evidence.

Two execution-environment observations were retained in the review evidence.
The standalone Release build log ended before all artefacts were listed despite
its reported zero exit; the subsequent current-image CTest fixture completed
the remaining compilation before the complete 44-test pass. The Release claim
therefore rests on that current-image gate and the complete JUnit/detailed logs.
Also, both full runs observed their disposable copies absent at completion, but
a later workspace check found the expected written Debug FAT16 copy reappeared.
Its origin was not established. Only that generated copy was removed after all
guest work ended, and a final check found all four Debug/Release FAT16/raw-block
copies absent. Source fixtures remained unchanged. Separate reconstructed-package
checks below also exercise ordinary cleanup and cleanup after a failed build.

## Native acceptance areas

| Area | Evidence checked |
| --- | --- |
| Successful updates | First/last byte, unaligned and fragmented ranges, nine touched sectors, full-medium comparison and unchanged input/guards |
| Repeated use | Multiple writes and fresh ownership proofs in one lease, cumulative requests beyond a single nine-sector plan, pending flush state |
| Range/attribute refusal | Existing size only, EOF/extension/overflow, empty file, directory, missing file, read-only targets and archive profile |
| Allocation isolation | Shared first clusters, shared tails, file/directory overlap, repeated directory references, cycles and orphan predecessors |
| Strict metadata admission | Canonical dot entries, parent links, root labels, deleted records, live LFN refusal, reserved/bad allocation, clean/error bits and full-FAT padding comparison |
| Finite work | Depth, directory, object, chain, FAT-size and total sector-read limits |
| Preparation failures | First/final sector read errors and final preparation deadline/clock rollback, with zero WRITE submissions |
| Mutation failures | Published first/middle/last sector errors, partial visible changes, exact confirmed/submitted counts and whole-sector uncertainty |
| Local refusal after progress | A known completed prefix followed by an unsubmitted next request, reported as PARTIAL_CONFIRMED |
| Flush and time | Failed and successful flushes, sticky earlier uncertainty, request deadlines and final enclosing clock/deadline rejection |
| Ownership | Owner/domain/DMA/input/result/path overlap, alignment, overflow, copied owners, busy state and callback reentry |
| Cleanup | Failed admission, retained reset, partial frame release, retryable Close, single admitted lifetime and no implicit flush |
| Simulated loss | Unflushed visible bytes can disappear; flushed model bytes survive; neither outcome creates an atomicity claim |
| Console | Command arguments, persistent state, shell ownership, refusals, historical result labels, uncertainty and cleanup |

Initial test expectation corrections retained the established VFS writable-mount
refusal, exact first-frame release failure semantics and the enclosing updater
read budget. The updater's 4096-reader-call bound includes inspector Open;
the planner also has its own counter. Exhausting the enclosing reader reports
its transport timeout, while a direct planner limit test reports DISK_LIMIT.

## Actual QEMU writer and independent readback

The immutable source is `tests/disk_inspection/fixture.raw`, an 8,388,608-byte
synthetic MBR/FAT16 disk. Its SHA-256 remains:

```text
bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938
```

The earlier raw block fixture is also unchanged:

```text
tests/virtio_block/fixture.raw
e5bea1290b3be59bdaf9f3a99d0be7527baefb3822b7d4b4e1c6c9736a4efa46
```

The new CTest graph creates only
`build/<configuration>/fixtures/umicom-fat16-update.raw`. The writer attaches
that copy using modern VirtIO, 512-byte logical/physical sectors, one queue,
`readonly=off`, `write-cache=on` and `cache=writeback`. It requires FLUSH support.
The source fixture is never attached writable.

The selected transformation is `/FRAG.BIN`, file offset 511, 700 bytes, using
`(i * 73 + 0x5d) & 255` for each patch index. It spans non-contiguous clusters
4, 9 and 6. The physical requests target LBAs 2179, 2184 and 2181; their requested
byte contributions are 1, 512 and 187.

The writer verifies every original disk byte, checks read-only and fixed-size
refusals, performs the update, compares every resulting byte, explicitly flushes
and closes. It checks unchanged caller input, guards, frame accounting and the
observed machine-control state. A second QEMU process then attaches the same
copy read-only. It checks the full disk and reads FRAG, README and GUIDE through
the existing VFS, including unchanged file size/EOF and refused write rights.

Required guest markers are:

```text
UMICOM_KERNEL_FAT16_UPDATE_READY
UMICOM_KERNEL_FAT16_UPDATE_READBACK_READY
```

The fresh guest does not reuse the first process's block queue, file provider or
RAM cache. The separate host C verifier also confirmed exactly 700 changed
bytes across the full 8 MiB and no other changes. This is orderly file-backed
persistence evidence. Physical storage power cuts, torn-sector guarantees and
on-disk uncertainty recovery remain outside this qualification.

## Process and fixture gates

| Stage | Required condition |
| --- | --- |
| `kernel.build.current` | The current source builds successfully |
| `kernel.riscv64.fat16_update_prepare` | Current-image success, then a fresh source-fixture copy |
| `kernel.riscv64.fat16_update` | Current image and prepared copy; real QEMU exit zero, exact writer READY then END |
| `kernel.riscv64.fat16_update_readback` | Current image, prepared copy and successful writer; real QEMU exit zero, exact readback READY then END |
| `kernel.riscv64.fat16_update_cleanup` | Own removal of the disposable copy, even if the current build or guests failed |

Filtered readback automatically includes the other four required stages.
Preparation and cleanup have ten-second CTest bounds; both guests have finite
process and enclosing CTest bounds. Cleanup deliberately has no successful-build
prerequisite, because a failed new build must still remove an earlier stale copy.

The process gate was tested independently with eight controlled subprocess
cases: valid writer and reader accepted; marker followed by exit 7, missing
marker, suffixed marker, explicit failure text, END before READY and marker
followed by timeout all refused. The new gate therefore cannot turn one of
those unqualified runs into a passing guest test.

The tests use no `snapshot=on` or `-snapshot`. QEMU documents unsafe caching in
snapshot mode, which would undermine this FLUSH qualification. The relevant
references are [QEMU cache options](https://www.qemu.org/docs/master/system/invocation.html)
and [VirtIO block WRITE/FLUSH](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

## Normal-system console qualification

The actual normal system image was also run with a separate disposable FAT16
copy. Its real command parser handled help/about, reported slot 7, refused a
read-only file, wrote the 18-byte text `Umicom file update` at FRAG offset 511,
reported current and historical flags, flushed, closed and powered off.

A separate run powered off while the successfully flushed updater lease was
still OPEN, exercising the shell teardown path. Independent full-image checks
for both runs found exactly the 18 intended changed bytes. Original fixture
hashes remained unchanged, and all created copies were removed after checking.

Use the slot actually reported by `disks` on another run; slot 7 is an observed
test result, not a general device-selection rule. The packaged fixture uses
primary-partition slot 0.

## Merge and build from PowerShell

Before overlaying files, check that the local checkout is the expected clean
`main` baseline. Resolve any existing local work deliberately before overwriting
its files:

```powershell
Set-Location "C:\umicom\umicom-kernel"
git branch --show-current
git status --short
git rev-parse HEAD
```

Expected HEAD is `ffe27d81672b4836ef67a24ca2a62ca7a5bb742d`. If remote `main`
has moved, compare the new commits with the review before overlaying this
baseline-specific delivery. Do not force-reset another change to make it fit.

After downloading, compare the archive hash with the separate HTML review.
The ZIP contains its own `umicom-kernel` top-level directory, so its parent
destination is `C:\umicom`:

```powershell
Get-FileHash "$HOME\Downloads\Umicom_Kernel_FAT16_Existing_File_Updates_and_Persistence.zip" -Algorithm SHA256
Expand-Archive -Path "$HOME\Downloads\Umicom_Kernel_FAT16_Existing_File_Updates_and_Persistence.zip" -DestinationPath "C:\umicom" -Force
Set-Location "C:\umicom\umicom-kernel"
git status --short
git diff --check
git diff --numstat

cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Stop at a failing command. The complete configured guest suite should contain 44 tests, including the new FAT16 writer/readback stages.
QEMU must be installed and discovered during configuration; a build without
QEMU does not earn emulator evidence merely by compiling the images.

The focused persistent-file lifecycle is:

```powershell
ctest --preset riscv64-clang-debug -R "kernel\.riscv64\.fat16_update_readback$" --output-on-failure --no-tests=error
```

This selects five tests through fixture dependencies, including a fresh build
check and cleanup. The generated writable copy should be absent afterwards.

Release can be configured independently with the same repository toolchain:

```powershell
cmake -S . -B build/riscv64-clang-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-clang.cmake
cmake --build build/riscv64-clang-release --parallel 2
ctest --test-dir build/riscv64-clang-release --output-on-failure --no-tests=error
```

## Optional interactive Windows check

Create a separate manual-test copy, then start the ordinary system image. This
uses the established MSYS2 UCRT64 QEMU location; substitute the installed QEMU
executable if it differs, retaining the displayed machine/backend flags.

```powershell
Set-Location "C:\umicom\umicom-kernel"
Copy-Item ".\tests\disk_inspection\fixture.raw" ".\build\riscv64-clang-debug\fixtures\umicom-fat16-update-manual.raw" -Force

& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
  -machine "virt,aclint=off" `
  -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
  -display none `
  -monitor none `
  -chardev "stdio,id=console,signal=off" `
  -serial "chardev:console" `
  -global virtio-mmio.force-legacy=false `
  -drive "file=./build/riscv64-clang-debug/fixtures/umicom-fat16-update-manual.raw,if=none,format=raw,id=umicom_fat_manual,readonly=off,cache=writeback,rerror=report,werror=report" `
  -device "virtio-blk-device,drive=umicom_fat_manual,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1" `
  -m 128M `
  -smp 1 `
  -no-reboot
```

At `umicom>`, replace 7 with the reported block-device slot if necessary:

```text
disks
fatwriteopen 7 0
fatwrite /README.TXT 0 "x"
fatwrite /FRAG.BIN 511 "Umicom file update"
fatwriteinfo
fatflush
fatwriteinfo
fatwriteclose
poweroff
```

The README operation should be refused as read-only. The FRAG update should
report 18 confirmed bytes and two completed sectors, with a pending flush until
`fatflush` succeeds. The historical write snapshot retains its original pending
flag; current state is shown separately. Closing never substitutes for flushing.

After QEMU exits, the manual image is available for independent inspection.
The synthetic source SHA should still match the value above. Remove only the
generated manual copy when finished:

```powershell
Get-FileHash ".\tests\disk_inspection\fixture.raw" -Algorithm SHA256
Remove-Item ".\build\riscv64-clang-debug\fixtures\umicom-fat16-update-manual.raw"
```

## Optional native qualification

The standalone native suite uses an ELF host linker's allocation/free wrapping.
The verified native host was Linux. These commands are for that host or an
equivalent WSL toolchain; they are not a claim that a native Windows linker
supports the same wrapping mechanism.

```sh
cmake -S tests/fat16_update -B build/native-fat16-update -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DUMICOM_FAT16_UPDATE_SANITIZERS=ON
cmake --build build/native-fat16-update --parallel 2
ctest --test-dir build/native-fat16-update --output-on-failure --no-tests=error
```

Use the reported sanitizer environment if running in the same restricted
process-inspection environment. The source suite reads its original fixture;
all mutation tests use process-private image storage.

## Commit and push on main

After review and successful local build/test, use the established explicit
commands, each only after the preceding command succeeds:

```powershell
Set-Location "C:\umicom\umicom-kernel"
git add -A
git diff --cached --check
git commit -m "feat(kernel): add checked FAT16 file data updates and persistence validation"
git push origin main
```

No branch creation or long PowerShell wrapper is required. Keep unrelated local
work out of the staged change after inspecting `git status` and the staged diff.

## Archive and review verification

The delivery review records the final archive SHA-256, full-file manifest and
original-line preservation audit. Packaging reconstructs a clean source tree
from the exact baseline plus the ZIP in a separate path containing spaces,
compares every source file byte for byte with the qualified tree, and builds
that reconstructed tree independently.

The extracted-source check runs the new native suite and the actual five-stage
QEMU lifecycle. A deliberate current-source compile failure must block copy
preparation and both guests while cleanup still removes a stale generated copy.
The original extracted source is then restored exactly and rebuilt. Only after
those gates pass is the archive and its self-contained HTML review delivered.
