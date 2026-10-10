# Read-only FAT16 recovery evidence for Umicom Kernel

**Author:** Sammy Hegab — Umicom Foundation  
**Licence:** MIT  
**Scope:** Native host-side diagnostics for disposable FAT16 disk images

This is an **entirely new, opt-in C23 host tool**. It is not a kernel module, does
not alter the production FAT16 inspector or VFS, and does not rewrite, disable
or remove any existing Umicom code. It is useful after exercising an
interruption in the Kernel's ordered FAT16 write/flush/Finish protocol.

## Why this tool is deliberately conservative

The existing Kernel FAT16 inspector already checks volume ownership and rejects
unacceptable metadata. This external tool has a different role: given an image
captured after a fault, provide simple repeatable evidence without taking a
Kernel device lease or asking the current filesystem to mount a damaged volume.

It checks:

- classic MBR signature and the explicitly selected primary partition 0–3;
- the permitted FAT16 partition types (0x04, 0x06 and 0x0E);
- supported 512-byte-sector FAT16 BPB geometry and bounded sector locations;
- reserved FAT entry values and the FAT16 clean-shutdown and no-I/O-error bits;
- **every sector of both FAT copies**, not just their header sectors;
- finite read budgets, unreadable sectors and malformed/truncated images.

The only external callback accepts a sector number and copies bytes **into** a
caller-owned output buffer. There is **no write callback, no repair function,
no file creation by the audit CLI and no automatic mutation**. The test fixture
maker is a separate test executable; it writes only its named disposable outputs
under the subproject build directory when invoked by the supplied CTest setup.

A result named `clean-mirrored-fat` means only that the examined FAT copies match
and their reserved header flags appear clean. It does **NOT** demonstrate complete
filesystem integrity, correct files or directories, successful application
write/commit semantics, reliable hardware flush, power-loss safety, or suitability
for an automatic repair. `dirty-or-io-error`, `fat-mirror-mismatch`,
`invalid-media-format`, `unsupported-fat16-profile`, and `incomplete-read` all
mean an operator must preserve the image and investigate independently.

The tool does not mount or repair the image, recover file bytes, scan the full
directory tree, detect every crosslink, or determine whether an unacknowledged
final clean write reached durable media. That requires additional evidence.

## Build on Windows (PowerShell and MSYS2 UCRT64)

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

cmake -S ".\tools\fat16-recovery-audit" `
      -B ".\build\fat16-recovery-audit" `
      -G Ninja -DCMAKE_BUILD_TYPE=Debug

cmake --build ".\build\fat16-recovery-audit" --parallel 2

ctest --test-dir ".\build\fat16-recovery-audit" `
      --output-on-failure --no-tests=error
```

The standalone test subproject registers five CTests, including 47 independent
recovery input cases and three CLI journeys on fresh disposable synthetic disk
files. It does not change the native Kernel CMake presets or their QEMU tests.

## Inspect a disposable raw-image copy

```powershell
& ".\build\fat16-recovery-audit\umicom-fat16-recovery-audit.exe" `
    ".\build\fat16-recovery-audit\audit-clean.raw" 0
```

A clean example returns JSON with `"classification":"clean-mirrored-fat"`,
`"differing_fat_sectors":0` and `"repair_performed":false` and exits 0.
A dirty or mismatched image exits 2. Unable to read necessary sectors exits 74;
invalid invocation exits 64; unreadable host file exits 66; malformed host
file size exits 65. An exit code of 0 is *not* a filesystem recovery approval.

Only use **disposable raw-image copies** created for validation. Never use a
physical drive, production disk, real user's VM image, or valuable data.
The host reader opens its selected input with `fopen(..., "rb")` and
rejects non-regular files (such as block devices and pipes).

The generated test images are retained under the selected build directory so
an engineer can inspect them; they are never committed. A local `git status`
should show only the newly added files under this `tools/` directory.

## Development boundaries

- C23, freestanding-compatible classifier core, portable host CLI.
- `UmicomFat16AuditInspect` has explicit source, callback, read limit and report.
- No Framework, GTK, database, Python or network dependency.
- The root `CMakeLists.txt`, Kernel files and all prior code remain unchanged.
- Host CTest evidence is not claimed as executed RISC-V guest or Windows evidence.
- More complete filesystem recovery, write-ahead repair strategy, power-cut
  injection in QEMU and installable OS recovery remain later work packages.

## Allocation-graph verification after header recovery evidence

A separate command, `umicom-fat16-integrity-audit`, now checks ownership of
FAT16 clusters reachable from the root and nested short-name directories. It
builds upon, but does not replace, the read-only header/mirror audit above.
A clean header alone cannot establish a non-overlapping allocation graph.

See [the allocation-integrity guide](ALLOCATION_INTEGRITY.md) for its precise
read-only scope, bounded traversal, PowerShell build and test commands, error
codes and known limitations. The original command syntax and JSON fields are
unchanged; the new CLI has its own output and test cases.

## Interruption laboratory: durable ordering and incomplete acknowledgement

The additional host-only executable `umicom-fat16-interruption-lab` now tests a
synthetic ordered FAT16 commit under both write-back and write-through device
models. It simulates power loss after every sector-write/flush boundary and
runs the existing header and allocation readers on only the remaining durable
bytes. It cannot write to a supplied image because it accepts no disk path.

The laboratory demonstrates why a clean FAT and a consistent allocation graph
cannot by themselves authorise replay, repair or even prove a file's previous
contents. A lost final acknowledgement may leave a fully updated clean image;
an intentionally unguarded partial data write can also leave a clean image.
Read [Interruption Qualification](INTERRUPTION_QUALIFICATION.md) before using
these diagnostics for recovery design. The Kernel's real VirtIO/FAT16 code is
unchanged; this test is model evidence, not a hardware power-cut test.

## QEMU normal-console Stage/Finish evidence (new, opt-in)

The `umicom-fat16-guest-commit-evidence` executable compares **every sector**
from the pinned, synthetic `tests/disk_inspection/fixture.raw` with a stopped
QEMU guest's disposable copy. It independently follows the `FRAG.BIN` FAT16
chain and checks the exact 21-byte console update at logical offset 511:

```text
fatstage /FRAG.BIN 511 "Umicom ordered update"
```

A verified *stage* must have precisely 23 changed bytes (21 file bytes and
one clean-flag byte in **each** FAT). A verified *finish* must have precisely
21 changed file bytes and fully original clean FAT headers. In both modes,
all other bytes—including unrelated files, allocation entries, directory
metadata and file slack—must be identical to the original fixture. The
existing header and allocation auditors are reused; no approval to repair,
replay, clear flags or mount unknown media is ever issued.

The companion `scripts/Test-UmicomKernelFat16CommitRecovery.ps1` runs four
actual normal-QEMU guest boots when explicitly invoked on a Windows machine
with RISC-V QEMU available. It kills **only its owned QEMU process** after
the guest positively reports a fully flushed Stage, verifies the dirty disk
and read-only rejection, then separately tests Finish and read-only reboot.
This is a process-termination/restart boundary after a confirmed flush, **not**
random in-flight interruption or proof of physical power-cut atomicity. See
[QEMU commit-recovery qualification](QEMU_COMMIT_RECOVERY.md) before executing.

The five extra standalone CTests use disposable host-generated images and
run without QEMU. They are **host evidence only**. QEMU acceptance is not
considered complete until the explicit PowerShell workflow runs successfully
and its `result.json`, logs and image SHA-256 values are reviewed.
