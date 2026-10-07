# Persisted FAT16 metadata inspection

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and boundary

The existing file committer records an explicitly supplied write calendar and
sets ARCHIVE when it updates file data. Its retained result is evidence about
that writer's operation. A later boot needs a separate read path to inspect the
directory words which are actually present on the medium.

`UmicomKernelFat16MetadataRead` provides that value snapshot through the
existing checked FAT16 inspector. The trusted console exposes the same work as
`fatstat SLOT PART PATH`. Neither interface depends on an old writer owner or
its result, and neither acquires permission to write or repair a disk.

This is part of the persistent-filesystem save/reboot/read gate in
`KERNEL_AND_OS_RELEASE_ROADMAP.md`. It preserves the native Kernel's independence
from the Framework, GUI and Data Server. The separate GNU Linux-libre production
direction remains as recorded in the existing architecture decisions.

## Public value types

The definitions are in `include/umicom/kernel/fat16_metadata.h`. Existing
`UmicomKernelFat16Entry` and VFS layouts remain unchanged.

| Value | Contents |
| --- | --- |
| `UmicomKernelFat16Timestamp` | Original `rawTime` and `rawDate` words, decoded calendar `value`, and explicit `state`. |
| `UmicomKernelFat16Metadata` | Existing `entry`, `directoryEntryPresent`, and `writeTimestamp`. |

The calendar type is reused from the file-planning header. Including it does
not create a planner, writable lease or commit lifetime. All results own their
values; they contain no pointer into a directory buffer or transport frame.

The timestamp state has three meanings:

| State | Meaning |
| --- | --- |
| `UMICOM_FAT16_TIMESTAMP_ABSENT` | Both raw words are zero. The reader reports the absence of a recorded calendar; it does not substitute an epoch. |
| `UMICOM_FAT16_TIMESTAMP_VALID` | The words encode a real Gregorian date and a valid hour, minute and even second. |
| `UMICOM_FAT16_TIMESTAMP_INVALID` | The words are not both zero and do not encode a valid calendar. Their raw values are retained for inspection. |

Only VALID populates the decoded `value`; every calendar field is zero for
ABSENT or INVALID. Midnight has a zero time word and is VALID when accompanied
by a valid date. Raw seconds fields 30 and 31 would encode 60 and 62 seconds and
are INVALID. They are not accepted as leap seconds. The Gregorian checks
distinguish the leap year 2000 from the non-leap year 2100.

The on-disk write time/date occupy entry bytes 22–25. The FAT format represents
years 1980–2107 and two-second time resolution. The reader decodes those fields
without a timezone conversion. These field locations and ranges follow the
Microsoft FAT specification cited below. ABSENT is this inspection interface's
explicit classification of zero words, not a claim that those words encode a
valid FAT date.

The qualification calendar written as `2037-11-23T14:35:59` is observed as
`2037-11-23T14:35:58`, raw time `0x747d` and raw date `0x7377`. The distinctive
date is a test value, not an instruction to use that date for ordinary files.

## Decoder contract

```c
UmicomKernelDiskStatus UmicomKernelFat16TimestampDecode(
    UmicomU16 rawTime,
    UmicomU16 rawDate,
    UmicomKernelFat16Timestamp *outTimestamp);
```

Every raw word pair is inspectable. With valid output storage the function
returns OK and publishes one of the three states. Malformed calendar content
is represented by INVALID, rather than losing the raw values in an error path.

The output must be non-null, naturally aligned and have a non-overflowing
extent. Invalid storage returns INVALID_ARGUMENT without changing any output
byte. The decoder performs no callback, allocation or media operation.

## Checked metadata query

```c
UmicomKernelDiskStatus UmicomKernelFat16MetadataRead(
    UmicomKernelFat16 *volume,
    const char *path,
    UmicomKernelFat16Metadata *outMetadata);
```

Open the inspector through its established read-only interface, keep the medium
immutable through Close, and provide stable trusted Kernel storage. The volume,
output and terminated path must have independent non-overflowing extents;
typed objects require natural alignment. This is not a user-pointer probing ABI.

The implementation checks each path byte for overlap before reading it. It
copies the bounded pathname before its first reader callback, acquires one
inspector busy lifetime and uses one total 4096-read query budget. It rechecks
the stored geometry before those fields become divisors or sector offsets.
The geometry check retains the ordinary read-only profile, including oversized
FATs which the existing Open accepts; the writer's separate FAT-sector limit
does not become a new metadata-read restriction.

Lookup decodes the complete parent directories needed by the path, validates
their bounded chains, and resolves the existing short alias. A second physical
pass re-identifies the selected entry before copying its timestamp. It compares
the name, attributes, size and first cluster with the checked lookup result.
The implementation does not accept a caller-supplied physical directory LBA.

The whole output, including padding, is published only after success. A reader
failure, corrupt directory, invalid owner or unsupported path leaves the
caller's previous bytes unchanged. A failing callback may damage its scratch
buffer; those bytes do not become a partial result. The path and local result
scratch are cleared before an admitted query ends, and the FAT cache is retired.

These guarantees depend on the existing callback contract: callbacks must not
edit owner/caller storage, retain pointers, re-enter the active inspector or
mutate the medium. The transport must impose a finite deadline in addition to
the parser's finite read count.

### Root and directory semantics

FAT16's root has no containing short directory record. Querying `/` returns the
existing synthetic root Entry, `directoryEntryPresent=false`, and a zero ABSENT
timestamp. It requires an already opened, valid inspector but performs no
additional media callback after owner and geometry validation.

A file or subdirectory has `directoryEntryPresent=true`, even when both time
words are zero. The subdirectory's timestamp comes from its record in its
parent, not from its `.` entry. Creation fields and access dates are outside
this query's contract.

### Integrity scope

The query follows ordinary Stat semantics. It validates the path and parent
directory chains; it does not newly prove the target file's complete data chain
or whole-volume allocation ownership. A metadata result is not evidence that
all file contents are readable, that every allocation is consistent, or that a
write can be admitted. The existing Read and write-planning interfaces retain
their own stronger, separately documented checks.

Long-name records are skipped under the established read-only rules. Deleted
records, labels and dot entries do not become selected short entries. Duplicate
aliases and malformed relevant directory records retain their established
refusal behaviour.

## Console lifetime

`include/umicom/kernel/disk_metadata_console.h` declares the standalone console
adapter. The existing disk command engine also accepts `fatstat`:

```text
fatstat SLOT PART PATH
```

The command acquires the established read-only block profile. That profile
requires a backend advertising read-only media; it refuses a writable backend
instead of opening a writable lease. In QEMU, attach the image with
`readonly=on` for this inspection lifetime. An active mounted/writer lifetime
must finish releasing its device before a fresh inspection can own it.

The adapter copies its bounded path before callbacks and holds the shared disk
busy guard from admission through final output. It counts the combined FAT
Open and metadata-query reads against one 4096-read command limit. It also
checks the monotonic ten-second deadline on the qualified 10 MHz platform,
including checks after the final query read and after resource release.

The private close path can retire the active interpretation and transport while
the public Close continues refusing callback reentry. A parsed `diskclose`
command also returns BUSY before attempting output through an active callback;
it cannot recursively report its own busy result through that same callback.

Metadata output is published only after the complete query and cleanup both
succeed. On failure after admission, the adapter emits a status line without a
metadata prefix. Pre-admission argument, busy and path-length refusals return
before output.
Reset/release failure retains the real handle for a later explicit `diskclose`
or the next inspection's initial cleanup attempt. It never abandons live DMA
storage. No WRITE or FLUSH request is part of this operation.

For a successfully committed fixture, the output includes:

```text
disk.file.name=FRAG.BIN kind=file bytes=1300 first-cluster=4 attributes=0x20
disk.file.read-only=0 hidden=0 system=0 directory=0 archive=1
disk.file.directory-entry=present
disk.file.write-state=valid raw-date=0x7377 raw-time=0x747d
disk.file.write-time=2037-11-23T14:35:58
disk.inspect=ok block=ok close=ok
```

ABSENT and INVALID appear explicitly in both the state and displayed time.
The raw words remain printed. Root is labelled `synthetic-root`; its synthetic
attribute byte is zero while its Entry kind is directory.

## Build and test

From the repository root, the normal RV64 preset builds the new console support
and the independent metadata-reader images:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The focused metadata selection also includes its required writer, preparation,
cleanup and current-image fixtures:

```powershell
ctest --preset riscv64-clang-debug -R "^kernel\.riscv64\.fat16_metadata" --output-on-failure --no-tests=error
```

QEMU must be discovered for guest tests to register. The no-tests option rejects
an empty selection; it does not enforce an expected inventory count by itself.
The native project is under `tests/fat16_metadata` and targets Linux/WSL, using
the established real-parser/transport models and ELF allocator wrappers.

Three fresh reader roles observe the source fixture, the completed file-commit
image and the staged dirty image. They share the established file-commit
fixtures rather than creating another writable copy. Cleanup therefore waits
for every registered reader which requires those fixtures. Each metadata
reader checks the entire image before and after its read-only lifetime, and
the host runner checks that QEMU left the complete image hash unchanged.

These guest results, the independent native cases and an actual ZIP-overlay
build are recorded in the accompanying delivery review. A clean process exit
alone is insufficient: the runner requires the exact role marker before
`UMICOM_KERNEL_END`, rejects failure markers and checks the process result.

## Reference

Microsoft, *Extensible Firmware Initiative FAT32 File System Specification:
FAT General Overview of On-Disk Format*, version 1.03, 6 December 2000,
directory structure and Date and Time Formats, document pages 23–26:
<https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf>.

The implementation is original project C23 code. The specification is a format
reference; no specification implementation or third-party filesystem source is
copied into this delivery.
