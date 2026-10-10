# Read-only FAT16 allocation and directory integrity audit

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

This addition extends the **existing host-only recovery evidence project** without
changing its original header/mirror audit executable or any Kernel/VFS source.
It is an optional, deliberately conservative *allocation graph* inspection.

## Why the first recovery audit is not enough

The earlier `umicom-fat16-recovery-audit` checks the partition geometry, FAT
mirror copies, reserved header entries and clean/I/O-error flags. A clean header
and matching mirrors do **not** establish that named files own disjoint chains.
For example, two directory records may refer to the same allocated cluster.
This second command checks that class of inconsistency before any recovery
planner or mount policy can responsibly be considered.

## Admission and evidence

1. Run the existing read-only audit on the same stable image. If its result is
   not `clean-mirrored-fat`, report `not-admitted` and do not traverse namespace.
2. Read the BPB again and check that the original admitted geometry remains
   consistent, then load and re-compare both FATs into a bounded snapshot.
3. Walk the fixed FAT16 root directory and discovered subdirectories, never
   interpreting an on-disk filename as a host path.
4. Claim every cluster in each live file and directory chain for one owner,
   detecting loops, cross-links, free/reserved/bad/out-of-range pointers and
   chains too short to hold a file's declared size.
5. Count allocated clusters with no reachable directory or file owner as
   **orphans**, without freeing or repairing them.

This is not a write, repair, mount, or rollback operation. A read-only command
cannot establish whether an interrupted operation is safe to replay. It cannot
prove the contents of files match a previous commit: a partial in-place write
can have a fully clean-looking metadata graph. The tool does not validate every
long-name sequence, duplicate names, timestamps, boot code, the contents of data
clusters, full directory metadata or every possible FAT16 variant. Concurrency
with an external writer is unsupported; work on a disposable stable copy.

The scanner uses a finite sector budget (maximum 32,768 *additional* reads),
a directory-entry budget (32,768), and at most 256 queued subdirectories.
The immutable callback takes `(context, sector, output[512])`; no mutating
callbacks exist. The heap allocation is bounded by the admitted FAT16 cluster
count (at most 65,524 data clusters) and the queue limit. Allocation failure,
read failure and work-budget exhaustion are explicit non-clean classifications.

### Return status

| Exit | Meaning |
|---|---|
| 0 | Consistent *observed* allocation graph; **not a recovery approval** |
| 2 | Unsupported/dirty/mismatched header, cross-link, loop, orphan or corruption |
| 64 | Bad command invocation |
| 65 | Unsafe/non-regular input or an invalid raw-image file size |
| 66 | Cannot open input file |
| 74 | Incomplete I/O or input close failure |
| 75 | Work limit or host allocation failure |

JSON reports `classification`, `header_classification`, `allocated_clusters`,
`referenced_clusters`, `orphan_clusters`, `crosslinks`, `loops`,
`invalid_chains`, `short_chains`, `invalid_entries`, read counters and
`repair_performed:false`.

## Build and test using PowerShell

Run these commands from the **repository root**, not inside `tools`:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

cmake -S ".\tools\fat16-recovery-audit" `
  -B ".\build\fat16-recovery-audit" -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { throw "FAT16 host configure failed" }

cmake --build ".\build\fat16-recovery-audit" --parallel 2
if ($LASTEXITCODE -ne 0) { throw "FAT16 host build failed" }

ctest --test-dir ".\build\fat16-recovery-audit" `
  --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "FAT16 host tests failed" }

$clean = ".\build\fat16-recovery-audit\audit-clean.raw"
$exe = ".\build\fat16-recovery-audit\umicom-fat16-integrity-audit.exe"
if (-not (Test-Path -LiteralPath $clean -PathType Leaf)) { throw "Missing synthetic disk: $clean" }
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing host executable: $exe" }
& $exe $clean 0
if ($LASTEXITCODE -ne 0) { throw "Unexpected non-clean result for the synthetic image" }
```

The existing header-only audit remains separately available at
`build\fat16-recovery-audit\umicom-fat16-recovery-audit.exe`.

For the independent RISC-V Kernel build (which does **not** build this host
subproject), remain at the root and run:

```powershell
cmake --preset riscv64-clang-debug
if ($LASTEXITCODE -ne 0) { throw "Kernel configure failed" }
cmake --build --preset riscv64-clang-debug --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Kernel build failed" }
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw "Kernel CTest failed" }
```

A failed command's actual diagnostic text is needed to determine its root
cause; three pasted commands without their output are not an error report.
The host tests cannot substitute for an RV64 QEMU/Windows regression run.

## Test evidence and limits

The test executable uses generated in-memory sector layouts rather than a disk
image made from private data. Scenarios include a clean root, files with
fragmented chains, nested directories, FAT reserved/bad cluster handling,
shared/cyclic chains, orphan allocations, short chains, invalid dot/parent
references, malformed metadata, I/O failures, work limits and a changed FAT
header between admission and traversal. CTest also reuses the three existing
synthetic raw files for acceptance/refusal of the command-line executable.

Every registered host test depends on an incremental build fixture, preventing
CTest from accepting results from a stale binary after a source compilation
failure. The original Kernel targets are not modified or overridden.
