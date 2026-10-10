# Interrupted FAT16 commits — deterministic host qualification

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and operational boundary

A Kernel has to distinguish *a write was requested*, *the device reported
completion*, *a flush made pending writes durable*, and *the final caller
received an acceptance result*. Those are four separate events.

This independent host-only laboratory models these distinctions in a small,
fully synthetic MBR/FAT16 image. It invokes the existing **read-only** FAT16
header and allocation audits after synthetic power loss. It adds no write,
mount or repair capability to either inspector or to the Kernel.

The model has no host path or disk-image input argument. It cannot access an
existing disk, USB device or VM image. Its media are short-lived heap arrays.
This is **not** proof of actual physical crash consistency, QEMU power-loss
handling, journalling, recovery, atomic write guarantees or safe automatic
replay. The system still needs independent real-device validation.

## Media and operation model

- 14,048 × 512-byte sectors (synthetic, privately allocated).
- Primary MBR partition at sector 2,048 with 12,000 sectors.
- Two 32-sector FAT16 copies, 512 root directory entries and two sectors per
  cluster. A synthetic `STATE.TXT` owns cluster 2 and contains 700 bytes.
- An update replaces 700 bytes across **two** consecutive file sectors while
  preserving the remaining slack. Directory names, size, neighbours and FAT
  cluster allocation remain unchanged.
- Write-back: completed WRITE changes only the volatile view; successful FLUSH
  copies that view to durable media.
- Write-through: each completed WRITE can immediately change durable media;
  FLUSH is still an independently acknowledged operation.
- The simulated failure loses all volatile cache contents. Read-only auditors
  inspect only the durable snapshot after the simulated restart.

Ordered steps (stop after 0 means no submitted write):

| Step | Completed operation | Result before any further operation |
|---:|---|---|
| 1 | Write dirty mirror FAT header | Not necessarily durable on write-back |
| 2 | Flush dirty mirror | Mirror dirty guard durable |
| 3 | Write dirty primary FAT header | May persist before flush on write-through |
| 4 | Flush dirty primary | Both dirty guards durable |
| 5 | Write first file sector | May have half-updated file data |
| 6 | Write second file sector | Both data sectors submitted |
| 7 | Flush file sectors | Two new data sectors durable |
| 8 | Write clean mirror FAT header | Primary remains the dirty guard |
| 9 | Flush clean mirror | Mirror and primary headers differ |
| 10 | Write clean primary FAT header | Could immediately become clean |
| 11 | Flush clean primary | Both FAT headers now clean |
| 12 | Verify clean headers and readback | Final caller may still lose acceptance |

A clean-header outcome without a confirmed write is counted separately only
when the model previously published at least one WRITE. Initial, untouched
images do not inflate that ambiguity counter.

The tool covers **28 scenarios**: interruption after steps 0–12 in both cache
models, plus an unacknowledged final acceptance in each model.

It separately demonstrates an *unguarded external write*: only the first data
sector changes, while both FAT copies stay clean and the allocation graph
stays consistent. Neither existing inspector can detect that application-level
hybrid by checking metadata alone. An independent expected-data hash or
transaction record would be required to identify the intended content.

## Evidence interpretation

| Observation | Appropriate conclusion | Forbidden shortcut |
|---|---|---|
| FAT copies disagree | Quarantine media for review | Choose one FAT without external proof |
| Dirty or I/O-error flag | Incomplete/error history is observable | Clear a dirty flag to make it mountable |
| Clean matching FAT, correct cluster graph | Only checked metadata appears consistent | Assume application data was committed |
| Clean headers, new bytes, lost final acknowledgement | Bytes may have committed without reply | Retry the update blindly |
| Clean headers, mixed bytes (unguarded write) | Metadata alone was insufficient | Treat a clean graph as recovery permission |

`repair_performed:false`, `recovery_authorised:false`, and
`operator_review_required:true` are hard-coded decisions. The latter remains
true even for normal completed synthetic cases because the observer is an
independent teaching tool, not the Kernel's transaction authority.

## Build and test from Windows PowerShell

This increment depends on the preceding allocation-integrity batch. If your
GitHub `main` has not yet received that previous batch, keep it in the local
checkout; do not extract an old repository snapshot over newer files.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

cmake -S ".\tools\fat16-recovery-audit" `
    -B ".\build\fat16-recovery-audit" -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { throw "Host configure failed" }
cmake --build ".\build\fat16-recovery-audit" --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Host build failed" }
ctest --test-dir ".\build\fat16-recovery-audit" `
    --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Host tests failed" }

& ".\build\fat16-recovery-audit\umicom-fat16-interruption-lab.exe" --matrix
if ($LASTEXITCODE -ne 0) { throw "Interruption matrix failed" }
& ".\build\fat16-recovery-audit\umicom-fat16-interruption-lab.exe" `
    --scenario write-through 5
& ".\build\fat16-recovery-audit\umicom-fat16-interruption-lab.exe" `
    --scenario write-back 12 --lost-ack
& ".\build\fat16-recovery-audit\umicom-fat16-interruption-lab.exe" --hybrid
```

All examples are synthetic and accept no file paths.

### Independent RISC-V Kernel regression

Run separately at the repository root (the host subproject is not integrated
into the Kernel target):

```powershell
cmake --preset riscv64-clang-debug
if ($LASTEXITCODE -ne 0) { throw "Kernel configure failed" }
cmake --build --preset riscv64-clang-debug --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Kernel build failed" }
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Kernel tests failed" }
```

Host tests cannot substitute for RISC-V/QEMU execution. No QEMU image or real
FAT16 filesystem bytes are modified by this increment.

## Design lessons and next work

A recovery design requires a trusted operation identifier, before/after
payload evidence, an independently verified publication/flush protocol and a
policy for choosing between roll-forward, rollback and quarantine. Those
contracts must be reviewed before any on-disk repair capability is added.
Power-cut injection against actual QEMU-backed writes, storage-device flush
behaviour and repeated mount/reboot are separate release gates.
