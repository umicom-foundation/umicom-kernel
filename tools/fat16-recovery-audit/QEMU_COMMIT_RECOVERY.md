# Ordered FAT16 commit: QEMU process-termination and reboot qualification

**Author:** Sammy Hegab, Umicom Foundation  
**Licence:** MIT  
**Target:** `umicom-kernel` RISC-V64 QEMU `virt,aclint=off`, one hart, 128 MiB  
**Category:** Opt-in destructive *synthetic* guest test, read-only host analysis

## Purpose and strict boundary

The previous interruption laboratory simulated WRITE/FLUSH durability in
private arrays. This qualification layer adds an independent on-disk byte
verifier and a PowerShell-controlled journey against the real *normal boot*
Kernel executable and actual VirtIO-backed raw disk files in QEMU.

There is **no kernel mutation**, FAT repair logic, flag clearing or replay.
Every historical source line in the two touched audit build/doc files remains.
The PowerShell script uses only copies of the existing synthetic fixture. It
accepts no arbitrary input disk path; it never opens an existing disk writable
through the host. No output file is deleted by the script on success or error.

**Not proved:** a kill after Stage has completed is not an injection in the
middle of a sector WRITE, a physical power cut or a guarantee about a dishonest
hardware backend. It does demonstrate that the existing Kernel-issued FLUSH
barrier persists the staged FAT guards/data through an abrupt **emulator
process** termination with the declared QEMU backend and that a subsequent
read-only guest refuses the dirty volume. Earlier model tests separately
cover cuts at each protocol phase, without claiming hardware evidence.

## What is checked, end to end

| Step | Evidence and fail-closed rule |
| --- | --- |
| Input admission | The immutable 8,388,608-byte `tests/disk_inspection/fixture.raw` must match SHA-256 `bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938`. |
| Two separate test images | Fresh UUID-scoped copies in the Kernel build output; never overwrite another run or user image. |
| Writer Stage | Start `umicom-system.elf` with modern VirtIO write-back and FLUSH. Discover the actual block slot, send `fatcommitopen`, then `fatstage /FRAG.BIN 511 "Umicom ordered update"`. Require exact successful results and the dirty-state notice. |
| Deliberate termination | Kill only the Process object created by this test, *after* the Stage response; wait for release before inspecting its image. Do not describe this as mid-write power loss. |
| Staged image | Independent audit says `dirty-or-io-error`, integrity audit says `not-admitted`, exact 8-MiB verifier reports **23** changed bytes; no unrelated byte changed. |
| Fresh read-only guest | Boot the same staged image read-only and require `mountdisk` to refuse. Power off and compare complete before/after image hashes. |
| Separate finished writer | On a **different** fresh copy, run Stage followed by successful `fatcommit`, `fatcommitclose`, `poweroff`. |
| Finished image | Both existing auditors admit clean matching FAT and consistent allocations. Exact verifier sees only the **21** expected file bytes changed. |
| New read-only boot | Remount the finished copy using a read-only QEMU block device, list root, unmount, poweroff; bytes must not change. |
| Retained evidence | JSON result record, per-boot serial/standard logs, two raw images, firmware and fixture hashes. Source fixture verified unchanged again at end. |

The independent verifier derives two physical data sectors from the FAT16 BPB,
root short-alias entry and FAT chain. It does **not** assume the fragmented file
occupies consecutive sectors. It compares **all 16,384 sectors**, not just the
changed locations. Expected positions for the checked synthetic fixture are
sectors 2179 and 2184, which cross a FAT cluster-chain boundary. Its public
API is a read-callback-only host diagnostic: no write/repair callback exists.

## Windows PowerShell setup

Open PowerShell in the existing repo. Confirm your local tree contains the
previous allocation-integrity and interruption batches. This batch does not
re-add or overwrite those files.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

git status

cmake -S ".\tools\fat16-recovery-audit" `
    -B ".\build\fat16-recovery-audit" `
    -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { throw "Audit configuration failed" }

cmake --build ".\build\fat16-recovery-audit" --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Audit build failed" }

ctest --test-dir ".\build\fat16-recovery-audit" `
    --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Audit tests failed" }
```

Expected standalone CTest set after adding this batch: **22/22**. CTest
creates its **own** four disposable image files in the build directory.
Their hashes are unchanged by the CLI tests.

You can run the full-image verifier directly on those generated test files:

```powershell
& ".\build\fat16-recovery-audit\umicom-fat16-guest-commit-evidence.exe" `
    ".\build\fat16-recovery-audit\guest-evidence-before.raw" `
    ".\build\fat16-recovery-audit\guest-evidence-staged.raw" stage

& ".\build\fat16-recovery-audit\umicom-fat16-guest-commit-evidence.exe" `
    ".\build\fat16-recovery-audit\guest-evidence-before.raw" `
    ".\build\fat16-recovery-audit\guest-evidence-finished.raw" finish
```

Both must return exit code `0`, classification `verified`,
`full_image_compared:true`, `unexpected_bytes:0`, and
`repair_performed:false`. Counts are 23 (stage) and 21 (finish).
`guest-evidence-corrupt.raw` must instead return nonzero.

## Build and exercise the normal Kernel in actual QEMU

```powershell
Set-Location "C:\umicom\umicom-kernel"

cmake --preset riscv64-clang-debug
if ($LASTEXITCODE -ne 0) { throw "Kernel configuration failed" }

cmake --build --preset riscv64-clang-debug --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Kernel build failed" }

ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Kernel regression failed" }

& ".\scripts\Test-UmicomKernelFat16CommitRecovery.ps1"
if ($LASTEXITCODE -ne 0) { throw "QEMU commit recovery qualification failed" }
```

Use `-QemuPath "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe"` and
`-BuildDirectory ".\build\riscv64-clang-debug"` if autodetection differs
from your installation. No additional disk-generation scripts, QEMU monitor
interactions or manual paste into the emulator are required.

The runner exits `0` only after all four QEMU boots, both host auditors, both
full-image comparisons and file-hash preservation checks succeed. It prints
an evidence directory under `build/riscv64-clang-debug/fat16-recovery-qualification/`.
The `result.json` inside it must show `passed:true`. The normal Kernel CTest
suite and this separately invoked acceptance runner have **distinct** evidence.

## Interpreting failures

- **Missing audit `.exe`** — build the standalone host subproject first. Do not
  assume that building the freestanding Kernel also builds host diagnostics.
- **Missing `umicom-system.elf`** — build the normal RISC-V preset; a successful
  build of a dedicated guest test image is not a substitute.
- **Source hash differs** — stop; the chosen fixture is wrong or was changed.
  In particular, `tests/fat16_file_commit/fixture.raw` is *not* the same as
  `tests/disk_inspection/fixture.raw`: its initial FRAG.BIN attributes differ.
- **Wrong slot** — do not hard-code slot 7; `disks` discovers one modern block
  device and the runner uses its printed slot.
- **Read-only dirty mount succeeds** — security/integrity regression. Preserve
  the evidence folder; do not run a repair automatically.
- **Exact-image mismatch** — a clean header or allocation graph alone cannot
  override unexpected data, slack or directory bytes. Preserve the image and
  inspect the first unexpected sector/offset in the verifier's JSON.
- **Timeout** — the runner preserves its own disk and serial logs and kills
  only the QEMU instance it started. It never kills all QEMU processes.
- **Host CTests pass but QEMU fails** — host models are not permission to
  declare the guest behaviour working. Report the full serial log and result.

## Next work

Qualify unsignalled mid-WRITE and mid-FLUSH exits using deterministic fault
injection inside the real guest transport, not a sleep-based external kill.
Develop an explicitly versioned recovery decision record with trusted payload
identity and operator-controlled quarantine, before considering rollback,
replay or on-disk repair. A clean FAT by itself is never recovery permission.
