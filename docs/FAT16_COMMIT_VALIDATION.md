# FAT16 commit validation and integration

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Source baseline

This delivery extends remote `main` at
`45a4ba56e98f43822dee61cacd432e7cfa2733c4`, commit
`feat(kernel): add checked FAT16 file data updates and persistence validation`.
All 22 complete files in the preceding delivery matched that remote source
exactly. Its archive SHA-256 was
`f4ad779e979597c93abe31031ba0a1438ca4c2e8d6e55e8dad89994280215386`.

The source-preservation audit retains every original line in every modified
file. This batch introduces no branch or automatic remote push. The final
archive manifest and exact hashes are recorded in the separate HTML review.

## Qualification environment

The qualification toolchain uses Clang/LLD 18.1.3, GCC 13.3.0, CMake/CTest 4.4.4,
Ninja 1.13.2 and QEMU RV64 8.2.2. The native host is Linux. Clang Debug native
qualification enables AddressSanitizer and UndefinedBehaviorSanitizer; GCC
Release qualification does not enable sanitizers.

Leak scanning requires process inspection unavailable in the execution
environment. It is disabled, without disabling ASan/UBSan themselves:

```sh
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export LSAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

The emulator target is RV64 QEMU `virt,aclint=off`, 128 MiB RAM, one hart and
modern VirtIO MMIO. Writers use real disposable raw-image copies with
`cache=writeback`, `readonly=off` and FLUSH support. Independent readers reopen
the same files with `readonly=on`. Snapshot/unsafe-cache modes are absent.
The [QEMU invocation documentation](https://www.qemu.org/docs/master/system/invocation.html)
describes the relevant cache and drive options.

## Acceptance evidence

Qualification completed on 7 October 2026 with the results below. Registered
tests, actual exits, semantic markers and complete detailed logs agree; a
successful configuration or the presence of source files is not treated as a
test pass.

| Check | Observed result |
| --- | --- |
| New native commit suite, Clang 18 Debug with ASan/UBSan | 283/283 passed; no failures or skips |
| Same native suite, GCC 13 Release | 283/283 passed; no failures or skips |
| Established native storage and console regression suites, Clang Debug with ASan/UBSan | 502/502 passed |
| Directly affected existing FAT16 updater suite after the final refinements | 163/163 passed again |
| Complete RV64 QEMU Debug suite, final frozen source | 52/52 passed in 138.224 seconds |
| Complete RV64 QEMU Release suite, final frozen source | 52/52 passed in 85.323 seconds |
| Controlled process-runner acceptance and rejection checks | 12/12 behaved as required |
| Separate actual QEMU writers and fresh readers | All four roles passed; both complete 8 MiB images matched independent expectations |
| Ordinary interactive console with Finish and with staged shutdown | Both passed; complete images contained exactly 21 and 23 changed bytes respectively |

There are 785 distinct native cases: 283 new and 502 established. Running the
same 283 cases under a second compiler, repeating the existing 163-case suite
after refinement, or rerunning cases from the archive does not increase that
distinct count. The new suite includes 24 interruption cases across 12
mutation/flush boundaries with cached and immediate-persistence models, and 19
cases through the actual console parser.

Two independent on-disk directory fixtures exercise the inspection limit. One
Stage consumes exactly the permitted 4096 reads and succeeds. The other needs
4092 preflight reads where only 4091 remain available after reserving the three
data readbacks and two dirty-header readbacks. It refuses with INSPECTION_LIMIT
while READY, before issuing any WRITE or FLUSH. No test sets the production
read counter artificially.

The final complete guest runs retained 266 unchanged source/fixture inputs;
their inventories, JUnit records and detailed CTest logs contain the same 52
cases. Both configurations subsequently reported `ninja: no work to do.` The
original disk and raw-block fixture hashes remained unchanged.

### Execution-environment observations

Some preliminary standalone build invocations returned zero with progress
logs ending before the complete target inventory. The current-image CTest
prerequisite subsequently built the remaining targets. Acceptance therefore
uses the final complete test records, exact guest markers, unchanged source
hashes and subsequent no-work builds, rather than those initial exit codes.

Generated writable test copies were also observed after cleanup tests had
reported success. Their reappearance was not attributed to a specific cause.
After all final guest work, the eight named disposable Debug/Release copies
were removed explicitly and their absence was checked. The original fixtures
were unchanged throughout. This observation remains recorded; it is not
presented as evidence that an unexplained environment behaviour was fixed.

## Native protocol checks

The standalone suite uses the real block driver, allocator, MBR/FAT parser and
commit implementation with an independently instrumented device model. Its
visible and durable arrays are distinct. An event trace records actual reads,
writes, flushes, payloads and persistent snapshots, so an expected order is not
inferred solely from the production state enum.

It checks successful byte ranges and fragmented sectors; unchanged input and
whole-image preservation; strict preflight refusals; each metadata/data write
failure; all flush barriers; read errors and wrong full-sector readback; and
interruption at publication/flush boundaries. Write-through variants allow
unflushed writes to persist immediately, rather than assuming that unflushed
data disappears on loss of volatile state. Lost acknowledgements can also occur
after the model has already persisted a completed clean update.

Ownership cases cover the whole enclosing committer, its embedded updater,
domain, all retained DMA pages, input, path and result. Lifetime cases exercise
single-use admission, failed-open cleanup, serial callback reentry and retained
reset/frame-release failure. Console cases use the real parser and check
historical evidence instead of relabelling a refused operation as successful.

The previous data-only updater and relevant established storage/console suites
are qualified separately. Repeated executions under another compiler or from
the extracted archive are not counted as additional distinct test cases.

## Actual clean and unfinished images

The original synthetic source is `tests/disk_inspection/fixture.raw`, 8 MiB:

```text
bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938
```

`/FRAG.BIN` has 1300 bytes and a fragmented chain of clusters 4, 9 and 6.
The automated patch starts at file offset 511, has 700 bytes and uses the
independent pattern `(index * 73 + 0x5d) & 255`. Caller contributions to the three
sectors are 1, 512 and 187 bytes. The absolute LBAs are 2179, 2184 and 2181.

Both writer roles compare all 8,388,608 original bytes before any update and
check read-only/range refusals. Stage then yields two modified FAT clean bits
and exactly 700 changed file bytes. It submits two header sectors and three data
sectors, with three successful flushes. The complete writer calls Finish,
restores the two original header sectors, and verifies that only the 700 file
bytes differ from the original. Cumulative totals are four header sectors,
three data sectors and five flushes.

The interrupted writer deliberately omits Finish, closes the owner and exits.
Its read-only successor checks the complete staged image and confirms that the
existing provider refuses it with DISK_DIRTY and does not publish a VFS domain.
The completed writer's read-only successor checks the entire image and reads
FRAG.BIN, README.TXT and DOCS/GUIDE.TXT through the existing VFS, including file
sizes, EOF, unchanged output tails and refused write rights.

The independent native verifier `tests/fat16_commit/image_verify.c` can verify
the staged or committed whole-image expectations without using the
production planner or the committer's staged payloads. Exact final image hashes
and observed comparisons are recorded in the HTML review.

These are real guest writes followed by fresh read-only processes. The native
model supplies additional controlled interruption and acknowledgement-loss
coverage. Neither is described as physical power-loss testing on real hardware.

## CTest lifecycle and failure gate

The full configured guest suite contains 52 tests when QEMU is discovered.
The new module adds two independent fixture chains: prepare, writer, fresh
reader and cleanup. They share the existing current-image build prerequisite.

`cmake/Fat16Commit.cmake` also acts as the narrow process runner. It requires
actual QEMU exit zero, the exact role readiness line followed by the exact end
line, and no failure marker. An 80-second child timeout sits within a 90-second
CTest timeout. A correct-looking marker cannot override a failing process exit.

Cleanup has no successful-build prerequisite. An intentionally failed current
build must block preparation and guests while cleanup still removes stale
disposable images. The source fixture itself is never attached writable.

## Windows: inspect, merge and build

Check your existing checkout before overwriting any files:

```powershell
Set-Location "C:\umicom\umicom-kernel"
git branch --show-current
git status --short
git rev-parse HEAD
```

Expected branch is `main`, working tree is clean and HEAD is
`45a4ba56e98f43822dee61cacd432e7cfa2733c4`. If the checkout differs, compare
those changes before applying this baseline-specific package. Keep your usual
Beyond Compare full-file review if you have local changes.

The archive contains its own `umicom-kernel` top-level directory. Its extraction
destination is the parent `C:\umicom`:

```powershell
Get-FileHash "$HOME\Downloads\Umicom_Kernel_Ordered_FAT16_Commits_and_Interruption_Detection.zip" -Algorithm SHA256
Expand-Archive -Path "$HOME\Downloads\Umicom_Kernel_Ordered_FAT16_Commits_and_Interruption_Detection.zip" -DestinationPath "C:\umicom" -Force
Set-Location "C:\umicom\umicom-kernel"
git status --short
git diff --check
git diff --numstat

cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Compare the archive hash with the HTML review before extracting. Run each
command after its predecessor succeeds. QEMU must be installed and discovered;
a compiled image without an emulator run is not a guest test pass.

The focused clean-commit and deliberate-interruption checks include their
automatic build, preparation and cleanup prerequisites:

```powershell
ctest --preset riscv64-clang-debug -R "kernel\.riscv64\.fat16_commit_(readback|rejected)$" --output-on-failure --no-tests=error
```

This selects nine tests: one shared current build and both four-stage chains.
For Release:

```powershell
cmake -S . -B build/riscv64-clang-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-clang.cmake
cmake --build build/riscv64-clang-release --parallel 2
ctest --test-dir build/riscv64-clang-release --output-on-failure --no-tests=error
```

## Optional interactive Windows check

Create a separate manual image and launch the ordinary console. The displayed
QEMU path follows the established MSYS2 UCRT64 installation; use the actual
installed path if it differs, retaining the machine and drive settings.

```powershell
Set-Location "C:\umicom\umicom-kernel"
Copy-Item ".\tests\disk_inspection\fixture.raw" ".\build\riscv64-clang-debug\fixtures\umicom-fat16-commit-manual.raw" -Force

& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
  -machine "virt,aclint=off" `
  -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
  -display none `
  -monitor none `
  -chardev "stdio,id=console,signal=off" `
  -serial "chardev:console" `
  -global virtio-mmio.force-legacy=false `
  -drive "file=./build/riscv64-clang-debug/fixtures/umicom-fat16-commit-manual.raw,if=none,format=raw,id=umicom_fat_commit_manual,readonly=off,cache=writeback,rerror=report,werror=report" `
  -device "virtio-blk-device,drive=umicom_fat_commit_manual,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1" `
  -m 128M `
  -smp 1 `
  -no-reboot
```

At `umicom>`, replace 7 with the slot shown by `disks` if necessary:

```text
disks
fatcommitopen 7 0
fatstage /README.TXT 0 "x"
fatstage /FRAG.BIN 511 "Umicom ordered update"
fatcommitinfo
fatcommit
fatcommitinfo
fatcommitclose
poweroff
```

The README refusal changes no disk byte. The successful FRAG Stage reports 21
confirmed caller bytes, two completed data sectors, two metadata sectors and
three flushes. Finish reports an accepted commit, four cumulative metadata
sectors and five flushes. Historical dirty observations remain visible after
clean finalisation; they are not a claim that the current disk is still dirty.

For deliberate interruption, repeat from a fresh manual copy but omit
`fatcommit`. Close/poweroff leaves the dirty indication. The automated rejected
reader is the independent qualification of that persisted state; it is not
replaced by a mere console success message.

After QEMU has exited, remove only the generated manual image when finished:

```powershell
Get-FileHash ".\tests\disk_inspection\fixture.raw" -Algorithm SHA256
Remove-Item ".\build\riscv64-clang-debug\fixtures\umicom-fat16-commit-manual.raw"
```

## Optional native Linux or WSL qualification

The native harness uses an ELF host linker's allocation/free wrapping. These
commands describe the verified Linux host or a suitable WSL environment; they
do not claim that the same wrapping works with a native Windows linker.

```sh
cmake -S tests/fat16_commit -B build/native-fat16-commit -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DUMICOM_FAT16_COMMIT_SANITIZERS=ON
cmake --build build/native-fat16-commit --parallel 2
ctest --test-dir build/native-fat16-commit --output-on-failure --no-tests=error
```

## Commit and push on main

After full-file review and successful local build/test, run each command only
after the preceding one succeeds:

```powershell
Set-Location "C:\umicom\umicom-kernel"
git add -A
git diff --cached --check
git commit -m "feat(kernel): add ordered FAT16 commits and interruption detection"
git push origin main
```

Inspect the staged files so unrelated local work is not included. No branch,
loop, PowerShell function or additional repository wrapper is required.

## Delivery reconstruction

The final ZIP is overlaid on a clean archive of the exact baseline in a separate
path containing spaces. All reconstructed source files must match the qualified
working tree, including files not present in the incremental full-file ZIP.
That reconstructed source must build and pass the new native suite and both
real QEMU fixture chains. A deliberate current-source compile failure must block
the guests while cleanup removes stale copies. Restore the packaged source and
rebuild before accepting that negative check.

The HTML review includes the exact archive SHA-256, complete source-file hashes,
the original-line preservation audit and the observed reconstruction results.
