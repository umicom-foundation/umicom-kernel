# Umicom Kernel — Filesystem provider validation and Windows integration

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Baseline and preservation

The complete-file source overlay is based on main commit
`94918f9883136ccda034b2c15fb81032a60dfdb5` in
`umicom-foundation/umicom-kernel`, containing checked primary partitions and
read-only FAT16 inspection. The remote main reference was checked again before
packaging and still named that commit. No branch, commit or push was created
in the validation workspace.

Nine previously tracked files receive additions only:

| Existing file | Added lines | Removed or rewritten lines |
|---|---:|---:|
| `CMakeLists.txt` | 8 | 0 |
| `arch/riscv64/linker.ld` | 41 | 0 |
| `cmake/HardwareDiscovery.cmake` | 1 | 0 |
| `include/umicom/kernel/vfs.h` | 11 | 0 |
| `kernel/console_shell.c` | 21 | 0 |
| `kernel/hardware_boot.c` | 28 | 0 |
| `kernel/main.c` | 7 | 0 |
| `kernel/vfs.c` | 6 | 0 |
| `tests/hardware_catalogue/CMakeLists.txt` | 26 | 0 |
| **Total** | **149** | **0** |

Every original line, including its line ending, remains in its original order.
The direct firmware-reader call superseded by the owned-copy boot adapter is
retained in an explained `#if 0` block. No existing algorithm is physically
deleted. The strict DTB reader and its 82-case original test source remain
byte-for-byte unchanged. Existing VFS values are not renumbered, its operation
table and owner layouts are unchanged, and both previously supplied disk
fixtures are unchanged.

The archive contains complete files at repository-relative paths. It contains
the nine modified files and nineteen new files, including these two guides.
It contains no new scripts, compiled images, host toolchain or replacement
fixture. Extract it into the repository root. The separate delivery review
records the final ZIP hash, per-file hashes and reconstruction check results.

## Execution environment and limits of the evidence

Validation used Linux x86-64, Clang/LLD/LLVM 18.1.3, GCC 13.3.0, QEMU 8.2.2,
CMake 4.4.4 and Ninja 1.13.2. The repository's RV64 integer `lp64` freestanding
toolchain and strict warnings remain in use. CMake selects the compiler's
supported spelling for C23. Actual Windows execution and the owner's local
compiler/QEMU combination have not been performed here.

The native suites use AddressSanitizer and UndefinedBehaviorSanitizer where
the established project supports them. Existing host stack-switching paths
retain their documented UBSan-only boundaries; no new warning suppression or
sanitizer exclusion was introduced. Leak scanning was disabled because this
execution environment does not permit LeakSanitizer's process inspection.
This is not a leak-scanner pass. The mount tests and guest checks independently
verify physical-frame ownership and restoration.

## Results

| Verification | Result |
|---|---:|
| Complete RV64 Debug build | Passed |
| Complete RV64 Release build | Passed |
| Main Debug CTest, real QEMU | 36/36 passed |
| Main Release CTest, real QEMU | 36/36 passed |
| New filesystem suite, Clang Debug, ASan/UBSan | 70/70 passed |
| New filesystem suite, Clang Release, ASan/UBSan | 70/70 passed |
| New filesystem suite, GCC Debug, ASan/UBSan | 70/70 passed |
| Hardware suite with firmware-copy cases, Clang Debug, ASan/UBSan | 103/103 passed |
| Hardware suite with firmware-copy cases, Clang Release, ASan/UBSan | 103/103 passed |
| Hardware suite with firmware-copy cases, GCC Debug, ASan/UBSan | 103/103 passed |
| Remaining 22 established native projects, Clang Debug | 1,363/1,363 passed |
| Normal-system persistent console session, real QEMU | Passed; exit 0 |
| Normal-system poweroff while mounted, real QEMU | Passed; exit 0 |
| Filesystem image without a disk, real QEMU | Correctly failed; exit 135, no filesystem-ready marker |
| Fixture hash before and after guest reads | Unchanged |

The main CTest count includes the current-image build fixture and 35 emulator
invocations. Several historical acceptance tests boot the same cumulative
diagnostic image with different required markers; these are not 35 independent
implementations. All tests used QEMU's default generated DTB for final acceptance.
A manually canonicalised DTB was used only during diagnosis, not for these
reported final CTest and console passes.

Across all 24 native projects, the final Clang Debug run contains 1,536 distinct
registered cases: 1,363 established cases outside hardware, 103 hardware cases
and 70 filesystem cases. None were disabled or skipped in those final runs.

## New filesystem suite

The 33 provider cases compile the actual VFS, FAT16 provider, inspector and
primary-partition reader. They cover owner admission and single-use lifetime,
short-alias identity, distinct empty files, nested lookup, fragmented reads,
shared and independent positions, EOF and zero-byte reads, oversized requests,
atomic failure output, enumeration epoch/cursor preservation, unpinned eviction,
pinned capacity, stale node IDs and identity exhaustion.

They also check local cleanup, empty-client unmount refusal, copied-owner
refusal, corrupt/unsupported media, invalid paths, callback reentry and policy
refusal. Both ordinary read-only client grants and deliberately broad trusted
clients are tested: mutation callbacks always refuse.

The 37 transport and console cases compile the actual mount, console adapter,
guest C validation, VirtIO driver, platform policy and physical allocator. They
reuse the previous VirtIO suite's unchanged MMIO/DMA model instead of replacing
the driver or writing a second device model. Each process reads a private mutable
copy of the existing synthetic fixture; the packaged fixture is never modified.

The cases cover persistent transport reuse, mount idle time, empty clients and
open files, duplicate handles, extra provider pins, a second VFS root pin,
read-only rights, unsafe contexts, slot exclusion, absent/malformed media,
allocation failures, refused reset, partial frame-release failures and retries.
The last successful sector crossing a deadline or moving the clock backward
must fail before caller bytes are published. Stalled clocks remain bounded by
the underlying polling limit.

Console cases cover root/nested listing and complete file reads, an active mount
surviving a repeated mount command, retry after no-device/bad-media/platform
refusal, unmount and poweroff cleanup retries, binary escaping, invalid arguments
and another console attempting to disturb the owner.

Native execution of the guest C routine is model evidence. The separate real
QEMU acceptance confirms that its calls also work through guest MMIO, DMA,
RISC-V fences, reset and startup on the qualified emulator configuration.

## Firmware regression coverage

The original strict DTB reader is unchanged. Twenty-one additional cases test
the owned-copy compatibility boundary, bringing the hardware project from 82
to 103 cases. They check exact property-padding-only changes, source immutability,
owned reader references, every truncated prefix, maximum-size admission,
capacity limits, all pairwise output overlaps, address overflow, typed-output
alignment, strict node-padding refusal and malformed headers/structures/values.
Failed validation clears the reader and count and wipes any copied extent.

A separate probe of the actual QEMU-generated DTB confirmed 26 nonzero property
padding bytes, with no other changed bytes. The original strict reader rejected
the producer's bytes and accepted the owned canonical copy, yielding 30 nodes
and 117 properties. Final guest boot uses the actual producer input and the
production boot adapter rather than that separately prepared diagnostic copy.

## Guest filesystem acceptance

`kernel.riscv64.disk_filesystem` uses its own image and the existing disposable
FAT16 disk. It requires `kernel_current_image`, a bounded timeout, modern
VirtIO MMIO and an explicitly read-only backend. An absent disk cannot produce
a successful filesystem marker. The acceptance prints:

```text
disk-filesystem.mount-and-client-lifetime=verified
disk-filesystem.directory-and-file-bytes=verified
disk-filesystem.fragmented-seek-and-duplicate=verified
disk-filesystem.mutation-rights=refused
disk-filesystem.reset-before-release=verified
disk-filesystem.frame-accounting=restored
disk-filesystem.machine-state=unchanged
disk-filesystem.disk-writes=none
disk-filesystem-test=pass
UMICOM_KERNEL_DISK_FILESYSTEM_READY
UMICOM_KERNEL_END
```

The machine-state observation covers the existing saved control-state fields;
it does not claim exhaustive observation of every external device register.
The fragmented file is checked for exact bytes across its non-contiguous chain,
not merely for a successful request status.

## Apply the complete files on Windows

Use the existing clean main checkout at `C:\umicom\umicom-kernel`. Check the
baseline and any local work before extracting full replacement files. If the
checkout has later changes in these files, merge the additions instead of
overwriting that work. No build-directory deletion is required.

```powershell
Set-Location "C:\umicom\umicom-kernel"

git status
git branch --show-current
git rev-parse HEAD

Get-FileHash "$HOME\Downloads\Umicom_Kernel_Read_Only_Filesystem_Provider_and_Mount_Lifetime.zip" -Algorithm SHA256

Expand-Archive -Path "$HOME\Downloads\Umicom_Kernel_Read_Only_Filesystem_Provider_and_Mount_Lifetime.zip" -DestinationPath "C:\umicom\umicom-kernel" -Force

git status --short
git diff --check
git diff --numstat

cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Compare the ZIP hash with the separate delivery review. Confirm the test output
contains 36 tests and that `kernel.riscv64.disk_filesystem` passes. QEMU must be
installed and discovered during configure; the existing build deliberately does
not fabricate emulator test registration when it is absent.

For focused reruns, the current-image fixture remains automatically included:

```powershell
ctest --preset riscv64-clang-debug -R "kernel\.riscv64\.(hardware_discovery|read_only_block|disk_inspection|disk_filesystem)$" --output-on-failure --no-tests=error
```

## Interactive Windows session

Use the normal system image and supplied file backend. This command uses the
established MSYS2 UCRT64 QEMU location; substitute the actual installed executable
path if that installation differs. Keep the displayed machine and backend flags.

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
  -machine "virt,aclint=off" `
  -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
  -display none `
  -monitor none `
  -chardev "stdio,id=console,signal=off" `
  -serial "chardev:console" `
  -global virtio-mmio.force-legacy=false `
  -drive "file=./build/riscv64-clang-debug/fixtures/umicom-fat16-read-only.raw,if=none,format=raw,id=umicom_filesystem_test,readonly=on" `
  -device "virtio-blk-device,drive=umicom_filesystem_test" `
  -m 128M `
  -smp 1 `
  -no-reboot
```

At `umicom>`, run `disks` and select the line with `device-id=2`. The validated
configuration used slot 7; use the reported slot rather than assuming it.
The fixture occupies primary-partition slot 0.

```text
disks
mem
mountdisk 7 0
mountinfo
diskls /
diskls /DOCS
diskcat /README.TXT
diskcat /docs/guide.txt
diskcat /FRAG.BIN
cat /README
unmountdisk
mountinfo
mem
poweroff
```

The disk README identifies the synthetic FAT16 fixture. `cat /README` still
reads the console's RAMFS README. `FRAG.BIN` displays escaped control bytes.
Mount info shows `mounted` with one client, then `closed` after unmount. The
allocated/free frame counts from the two `mem` commands must match. The console
uses one admitted mount lifetime per boot; restart for another successful mount.

After QEMU exits:

```powershell
$LASTEXITCODE

Get-FileHash ".\build\riscv64-clang-debug\fixtures\umicom-fat16-read-only.raw" -Algorithm SHA256
```

Expected exit status is zero. The fixture SHA-256 is:

```text
BD866D6AE337527F3A8B4609F31969D60185E025F8525726F573D905A2F50938
```

## Record the completed change on main

Once the displayed diff and local build/test results are satisfactory, use the
established explicit Git sequence. Review staged content before committing;
`git add -A` includes all intentional work in the checkout.

```powershell
git status
git add -A
git diff --cached --check
git diff --cached --stat
git diff --cached --numstat
git commit -m "feat(kernel): mount checked read-only FAT16 through VFS"
git push origin main
git status
```

No helper script, branch workflow or remote action is needed to apply the
delivery. The package implements the read-only provider and explicit mount
lifetime; mutable disk filesystems and user-process disk namespace grants remain
separate architectural work.
