# Umicom Kernel — Checked partitions and read-only FAT16 inspection

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## From a sector to a named file

The block driver already gives the Kernel a checked way to read sectors. A
sector is not a file: the Kernel must first establish where a partition begins,
then interpret that partition's filesystem geometry, directory records and
allocation chains. This implementation performs those steps without adding a
write request, running disk boot code or changing the existing RAM filesystem.

For example, `/DOCS/GUIDE.TXT` on the synthetic disk is resolved through an MBR
partition, the FAT16 root directory, a subdirectory cluster and finally the
file's cluster chain. It is not an embedded string returned by the command.

```text
Qualified read-only VirtIO block owner
                  |
                  v
Single-sector reader with a whole-device extent
                  |
                  v
Checked primary MBR table
                  |
                  v
Partition-bounded FAT16 interpretation
          |                       |
          v                       v
Copied directory records     Staged file bytes
          |                       |
          +-----------+-----------+
                      v
          Read-only console inspection
```

`disk_partitions.c` and `fat16_inspector.c` know nothing about MMIO register
addresses or DMA pages. Their reader supplies only a sector count and a read
callback. The existing block driver remains the authority for device admission,
queue completion, reset and DMA lifetime. No second transport is introduced.

## Why the partition boundary is checked first

A legacy MBR has four primary records. Each gives an LBA start and sector count;
its old cylinder/head/sector fields are not used for address calculations here.
The entire table is validated before the caller receives any accepted record.

A nonempty partition must start after the MBR sector, fit inside the device and
not overlap another admitted primary partition. An invalid final record cannot
leave three apparently usable records in the caller's output. Arithmetic uses
wide values and subtraction-based bounds checks rather than an overflowing
32-bit `start + length` test.

Slots keep their original indices, including holes. The interface uses indices
zero through three. The synthetic fixture's partition is slot **0**; this is
separate from the VirtIO device slot selected by the `disks` command.

A protective GPT record or an extended-partition container makes the table
unsupported. This implementation does not follow EBR chains, reinterpret a
protective range as FAT, or fall back to treating a malformed disk as an
unpartitioned volume. Unknown ordinary primary partition types can be displayed
by `partitions`, but are not automatically admitted as FAT16.

## The admitted FAT16 profile

Open reads the BPB as a little-endian byte protocol. Native structure alignment
and a compiler's padding rules do not determine where a disk field lives.
The filesystem label is metadata for display, not proof of filesystem type.

This profile requires:

| Property | Requirement |
|---|---|
| Primary partition type | `0x04`, `0x06` or `0x0e` |
| Sector size | 512 bytes |
| FAT copies | Exactly two |
| Sectors per cluster | A power of two from 1 through 64 |
| Reserved area and FAT length | Nonzero and within the volume |
| Root directory | 1–512 entries, in complete groups of sixteen |
| Total sector count | Exactly one nonzero BPB total-size field |
| Hidden-sector offset | Matches the partition's device start |
| Volume extent | Fits wholly inside the selected partition |
| FAT capacity | Contains entries for every derived data cluster |
| Cluster count | FAT16 range; additionally excludes reserved top cluster IDs |
| FAT reserved entries | Expected media descriptor and reserved-entry shape |
| Clean/no-I/O-error flags | Both set |

FAT16 is derived from the data-cluster count, not from a string saying `FAT16`.
The 4,085/65,525 classification boundaries are used, with a stricter upper check
that leaves the reserved cluster-number values outside the admitted data range.
This is not a claim that every historical formatter or operating system agrees
at the boundary. The fixture uses 12,159 clusters, away from those edges.

An unclean volume is refused. The inspector does not repair allocation chains,
select a preferred disagreeing FAT copy or set a clean flag on somebody else's
media. A read-only result must not hide a repair operation.

## Cluster chains are not contiguous extents

Cluster 2 denotes the first data cluster. A file starting at cluster 4 might
continue at cluster 9 and then cluster 6. Adding one to a physical sector number
is therefore not a substitute for following the FAT.

Before publishing a file read, the inspector validates the complete bounded
chain. Every next cluster must be in range and must not be free, reserved or
marked bad. A repeated cluster is a cycle. The end of chain must agree with the
number of clusters required by the directory's file length. Too short and too
long are both refused; an empty file uses no cluster.

The two FAT sectors are compared whenever a chain traversal consults them.
A disagreement is reported rather than arbitrarily choosing one copy. Cached
FAT bytes are invalidated between operations. The caller nevertheless promises
that the medium remains unchanged from Open through Close; this is not a lock
against another host process modifying the backing file.

The whole volume is **not** scanned. Unreferenced clusters, orphaned allocation,
file-to-file cross-links and all unused FAT sectors are outside this inspection.
`Stat` reports validated directory metadata; only `Read` validates the selected
file's complete bounded allocation chain. Listing a directory does not validate
every file beneath it. These are deliberately different levels of evidence.

## Bounded directory and pathname interpretation

The interface resolves canonical absolute paths using a portable short 8.3
alias alphabet. ASCII letters are case-folded, so `/docs/guide.txt` and
`/DOCS/GUIDE.TXT` select the same short alias.

The following are refused before directory I/O:

```text
relative/file
/DOCS//GUIDE.TXT
/DOCS/./GUIDE.TXT
/DOCS/../README.TXT
/DOCS/GUIDE.TXT/
```

There is no current directory, Unicode normalisation or guessed OEM code page.
Long-filename records are skipped and counted; they are not decoded. Where a
short alias exists, that alias remains visible. Deleted entries, volume-label
records and valid dot entries are not returned as ordinary files. Unsupported
short names and duplicate case-folded aliases cause explicit refusal.

A directory result is built in owner-local scratch storage before it is copied
to the caller. A late malformed record or read failure cannot publish a partial
listing as though it were complete.

| Bound | Value |
|---|---:|
| Primary records | 4 |
| Path bytes, excluding terminator | 255 |
| Path components | 8 |
| Returned entries per directory | 128 |
| Raw entries scanned per directory | 512 |
| Clusters in one inspected chain | 256 |
| Sector reads per public lookup/list/read operation | 4,096 |
| File bytes returned by one Read call | 4,096 |

These bounds intentionally refuse some valid large files and directories.
`inspection-limit` is not a claim that the filesystem is corrupt. Directory
allocation extent is bounded before enumeration, even when an early end marker
might make a larger allocated directory cheap to scan in practice.

## Output publication and EOF

Read accepts an offset and a capacity. The requested bytes are staged in the
owner's buffer. They reach the caller only after the complete requested range
has been read successfully. On failure, both the output bytes and the output
count remain unchanged. Unused capacity is not cleared or overwritten.

A successful read at or beyond EOF returns zero bytes. It does not return the
unused tail of the last disk cluster. The fixture deliberately fills that slack
with another byte pattern to make accidental disclosure detectable.

These C interfaces accept trusted Kernel pointers. The reader, its context and
medium must remain valid through Close. Start the owner in stable, zero-filled
storage; do not copy it while open. Caller result buffers must not overlap the
owner or its input storage. Public operations reject recursive use after entry;
callbacks must not re-enter during Open or mutate the owner. This is single-hart
serial execution, not a concurrent mount or a user-pointer ABI.

## Transport cleanup belongs to the old driver

An inspection command opens a device, interprets it, retires the interpretation,
then closes the original block handle. The core parser allocates no physical
frames. The transport retains its original queue and bounce-frame ownership.

A command-level error does not authorise throwing away the device handle. If
reset or release fails, the console keeps the token in stable storage and
`diskclose` can retry. A subsequent inspection also retries retained cleanup
before opening another device. Poweroff checks it before dismantling the
console's filesystem.

The device still receives control-register writes for negotiation, queue
submission and reset. **No disk-content write requests are added.** All
filesystem operations use only the original read-only block API.

The qualified console path also bounds the complete command to ten seconds on
the existing 10 MHz platform clock. Each block request retains its own deadline
and poll limit. The core I/O-call limit remains useful when a clock does not
advance. Neither limits nor timeouts prove that outstanding DMA has stopped;
that remains the block driver's reset-acknowledgement rule.

## The console commands

```text
partitions SLOT
fatinfo SLOT PART
fatls SLOT PART PATH
fatcat SLOT PART PATH
diskclose
```

`fatcat` displays at most one complete 4 KiB file. A larger file is refused
before printing a prefix. The C Read interface still supports offset reads of
larger files within the chain limit.

Directory aliases and labels are sanitised before display. Cat shows printable
ASCII, turns a newline into terminal CR/LF and escapes other bytes, including
ESC. Disk contents cannot issue terminal escape sequences.

Each completed command reports the interpretation, transport and cleanup result
separately, for example:

```text
disk.inspect=ok block=ok close=ok
```

The existing `hardware`, `disks`, `readsector`, `blockclose`, RAMFS commands and
program-launch paths remain available. The old raw-block report's obsolete
sentence saying that no partitions/filesystems exist is retained under an
explained disabled block. Its replacement describes the raw driver accurately;
no block logic is disabled.

## A synthetic partitioned disk, not a host filesystem

The packaged fixture is exactly **8,388,608 bytes**, with one primary FAT16
partition at sector 2,048 spanning 12,288 sectors. It contains:

```text
/README.TXT
/DOCS/GUIDE.TXT
/FRAG.BIN
/EMPTY.TXT
```

`FRAG.BIN` contains 1,300 deterministic bytes across clusters **4 → 9 → 6**.
The root, directory contents, data, two FAT copies and boot metadata are all
created by the included native C fixture builder. There is no copied host disk
content and no bootloader program in the MBR.

The complete raw-file SHA-256 is:

```text
BD866D6AE337527F3A8B4609F31969D60185E025F8525726F573D905A2F50938
```

CMake checks that hash and copies the image to:

```text
build/riscv64-clang-debug/fixtures/umicom-fat16-read-only.raw
```

Normal contributors do not need to run a formatter or build the fixture tool.
`tests/disk_inspection/fixture_builder.c` is supplied for reproducibility. It
creates a new output file exclusively and refuses an existing path. It is not a
general formatter; never pass a device path. No regeneration script is required.

## Keep the two fixture images separate

The earlier raw-block acceptance test requires its original 128-sector pattern.
Its assertions and binary fixture are unchanged. Running that cumulative image
against an unrelated FAT16 fixture would correctly fail the old raw-data test.

The new automatically terminating image is:

```text
tests/umicom-disk-inspection.elf
```

It reuses essential boot memory/trap setup and the actual hardware catalogue,
then runs the partition/FAT16 acceptance sequence against its own disk. The new
CTest is `kernel.riscv64.disk_inspection` and requires `kernel_current_image`.
It does not weaken an earlier acceptance marker to accommodate different data.

For an interactive inspection session, use **`bin/umicom-system.elf`**, not the
cumulative diagnostic or diagnostic-console image, with this disk attached.

## Manual build and qualification

Use the ordinary incremental configure, build and CTest commands. No clean
build tree or extra package installation is required. Then launch from the
repository root in PowerShell:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
    -display none `
    -monitor none `
    -chardev "stdio,id=console,signal=off" `
    -serial "chardev:console" `
    -global virtio-mmio.force-legacy=false `
    -drive "file=./build/riscv64-clang-debug/fixtures/umicom-fat16-read-only.raw,if=none,format=raw,id=umicom_fat_test,readonly=on" `
    -device "virtio-blk-device,drive=umicom_fat_test" `
    -m 128M `
    -smp 1 `
    -no-reboot
```

Use **only the supplied disposable fixture** for this qualification. Do not
substitute a physical disk or a file with valuable data. The QEMU read-only flag
does not protect against a separate host program editing the same file.

Run `disks` and use the slot which reports `device-id=2`. When it is slot 7:

```text
partitions 7
fatinfo 7 0
fatls 7 0 /
fatls 7 0 /DOCS
fatcat 7 0 /README.TXT
fatcat 7 0 /DOCS/GUIDE.TXT
poweroff
```

Partition zero should report type 6, start 2,048 and length 12,288. FAT geometry
should report 12,159 clusters and one sector per cluster. Root enumeration
returns README.TXT, DOCS, FRAG.BIN and EMPTY.TXT. The `DOCS` listing returns
GUIDE.TXT. A successful command ends with all three statuses equal to `ok`.

After poweroff, check `$LASTEXITCODE` and the raw-file hash. The expected exit
code is zero and the fixture hash must remain unchanged.

## What remains separate

There is no GPT or extended-partition support, FAT12/FAT32/exFAT support,
long-filename decoding, repair, format, disk write, journal or crash recovery.
The inspector is not a VFS mount and does not hand disk descriptors to user
programs. Nothing is imported into RAMFS and no executable is automatically run
from the inspected disk. Authentication, global consistency checking and host
concurrent-modification detection are also outside this implementation.

The next storage integration can add a checked read-only filesystem provider
with explicit mount lifetime. Writable persistence must separately establish
write ordering, failure handling and recovery; a successful read inspector does
not satisfy those requirements.

## Format references

The wire-layout facts were checked against these primary project/specification
sources. They are references, not source files copied into this implementation:

- UEFI specification, legacy MBR and protective MBR layouts:
  https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
- dosfstools maintainers' FAT geometry and BPB definitions:
  https://github.com/dosfstools/dosfstools/blob/master/src/mkfs.fat.c
- dosfstools maintainers' FAT type boundaries and boot-sector interpretation:
  https://github.com/dosfstools/dosfstools/blob/master/src/boot.c

These references do not imply UEFI firmware support, compatibility with every
historical FAT variation, or adoption of another project's implementation.
