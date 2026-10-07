# Umicom Kernel — Read-only filesystem provider and mount ownership

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Capability and architectural position

The checked FAT16 inspector now implements the existing typed VFS provider
contract. A mount owner keeps one read-only block-device handle alive while the
provider, its VFS root, clients and file descriptions depend on it. The console
can retain that mount between commands instead of opening and closing the
transport for every inspection.

Console and trusted Kernel clients use the existing VFS and descriptor rules.
The FAT16 provider connects those rules to the checked inspector and primary-
partition reader. Their sector reads use the mount-owned VirtIO block handle;
the existing transport and platform layers retain device-reset and DMA ownership.

The Kernel remains independent of the Umicom Framework and the Umicom OS
distribution. This work belongs to the native RISC-V Kernel research target;
it does not change the GNU Linux-libre basis of the production distribution.

## Source responsibilities

| Source | Responsibility |
|---|---|
| `include/umicom/kernel/fat16_provider.h`, `kernel/fat16_provider.c` | Typed VFS callbacks, canonical short-alias paths, node identities, metadata snapshots and pins |
| `include/umicom/kernel/disk_filesystem.h`, `kernel/disk_filesystem.c` | Block-handle acquisition, mount publication, read-only client grants, operation deadlines and reverse-order cleanup |
| `include/umicom/kernel/disk_filesystem_console.h`, `kernel/disk_filesystem_console.c` | One persistent console mount and bounded, staged terminal output |
| `kernel/disk_filesystem_validation.c`, `kernel/disk_filesystem_boot.c` | Isolated guest acceptance using the existing disposable FAT16 fixture |
| `cmake/DiskFilesystem.cmake` | Source integration, specialised image and real QEMU CTest registration |
| `tests/disk_filesystem/` | Actual provider, VFS, mount and console code exercised through the existing byte fixture and VirtIO model |

The existing partition parser and FAT16 inspector remain the only disk-format
interpreters. The provider does not add a second BPB parser or independently
follow FAT chains. The block driver remains responsible for device identity,
negotiation, request descriptors, completion validation, reset and frame release.

## A mount is an owned lifetime

Supply stable, initially zero-filled `UmicomKernelDiskMount` storage. Never
copy a live owner, clear it to force reuse, or let its address go out of scope
while a client or retained device handle can still refer to it. Calls use the
existing serial, trusted-Kernel execution contract.

`UmicomKernelDiskMountOpen` acquires the block handle directly into that final
owner. It then opens the checked FAT16 provider and mounts the existing VFS on
the provider's root. Publication occurs only after every admission step succeeds.

| State | Meaning and permitted next action |
|---|---|
| `UNUSED` | No admitted filesystem lifetime. Admission can be tried when all earlier transport ownership has been released. |
| `OPEN` | The VFS and provider are live. Read-only clients can be attached. |
| `CLOSING` | Admission or teardown retained cleanup obligations. Keep the owner alive and call close again. |
| `CLOSED` | An admitted single-use lifetime has ended. Use fresh owner storage for another Kernel mount. |

A failed open can still leave a real block handle in `CLOSING`. Its return value
describes the primary admission failure; `lastCleanupStatus` separately records
reset or release trouble. A failure is never permission to overwrite the handle.
Admission can be retried in the same owner only when no provider was admitted
and all earlier transport ownership has been released.

Once a provider has been admitted, the owner follows the existing VFS single-use
contract. Keeping descriptor generation tables intact prevents a reset owner
from accidentally making a stale identity valid again. The current console has
one such owner, so a successful mount followed by unmount requires a system
restart before another successful console mount. A Kernel component can instead
provide a different, fresh owner for a later lifetime.

## Clients, file descriptions and reverse-order release

`UmicomKernelDiskMountClientOpen` is the normal grant boundary. Its permitted
rights are `READ`, `QUERY`, `ENUMERATE` and `DUPLICATE`; requests containing
`WRITE`, `CREATE` or `REMOVE` are refused.

The mounted VFS pins the provider root. Independent open file descriptions add
their own node pins. Duplicated descriptors retain the established VFS behaviour:
they share one file description and its current position. Separately opened
descriptors have independent positions. The existing descriptor-generation,
principal and rights checks remain in the VFS.

Even an attached client with no descriptors makes mount close return `BUSY`.
Additional direct provider pins or a second trusted VFS mounting that provider
also prevent teardown. This refusal leaves the active mount, transport and
clients usable. It does not first close part of the namespace.

Successful teardown follows the dependency order in reverse:

1. The caller closes its descriptors and detaches all clients.
2. VFS unmount releases its root pin.
3. Provider close releases its interpretation and cached records locally.
4. Block close obtains device-reset acknowledgement, scrubs DMA storage and
   releases the owned physical frames through the existing driver.

An unacknowledged reset or a partial frame-release failure keeps the block
handle in the mount. A later close retries only the remaining cleanup. Closing
an unused or fully closed mount is harmless. Provider validation, metadata
queries, unpinning and close do not require another disk read, so media failure
cannot create a circular dependency in which successful I/O is needed to release
the failed I/O owner.

## Node identity, paths and immutable metadata

The provider has a fixed 64-record node cache. A cache position is storage; the
monotonically issued node ID is the identity. Reusing an unpinned position does
not reuse its former ID. Root ID 1 remains pinned by the mounted VFS, and an
open description's pin prevents eviction of its node. When every record is
pinned, or the identity counter is exhausted, the operation returns an explicit
failure rather than replacing a live record.

Paths use the inspector's bounded short-alias rules. ASCII aliases are folded
to uppercase for canonical identity: `/docs/guide.txt` and `/DOCS/GUIDE.TXT`
refer to the same cached pathname. Empty files remain distinct by pathname;
the absence of a starting data cluster is not a unique file identifier.

Metadata is an immutable snapshot for the mounted lifetime. Cached lookup and
stat need no media access. Directory enumeration stages a complete bounded
inspector result before publishing one entry. Its fixed nonzero epoch is valid
because the medium is required to remain unchanged. A wrong epoch is refused;
an unsuccessful enumeration publishes neither a new entry nor a new cursor.

The read-only device setting prevents this guest from writing. It does not stop
another host process from changing the backing file. The caller must maintain
the immutable-medium condition for the complete mount lifetime. Hot removal,
media-change detection and coherent host snapshots require separate contracts.

## Reads, limits and error reporting

The provider accepts at most `UMICOM_FAT16_READ_BYTES` (4,096 bytes) in one read.
The existing VFS controls seek positions and advances a shared file description
only on successful transfer. EOF is a successful zero-byte result; a valid
zero-length read needs no media access.

The inspector stages the complete requested result before copying it to the
caller. On backend failure the provider reports a zero transferred count and
preserves the caller's data buffer. It does not add another large read staging
buffer. Fragmented file chains, offset reads and cluster slack are interpreted
by the existing inspector.

The mount renews a monotonic time budget before each complete inspector call.
This avoids treating idle mount time as I/O time. Both the start and completion
of each sector are checked, including the last sector of an operation. Backward
clocks, elapsed deadlines and the existing finite sector-read limit fail before
caller data is published. Each block request also retains its own timeout and
finite polling bound. An in-flight request can finish after the encompassing
inspector deadline; its completion is then refused. A VFS pathname traversal
can involve several separately bounded inspector calls.

Five new status values are appended to the VFS status enumeration without
renumbering any existing value or changing callback signatures:

| Status | Meaning |
|---|---|
| `IO_ERROR` | The underlying read could not produce a checked result. |
| `READ_ONLY` | The provider or device policy refuses mutation. |
| `UNSUPPORTED` | The existing inspector or transport does not support the supplied format or feature. |
| `INSPECTION_LIMIT` | A deliberate work or buffer bound would be exceeded. |
| `CORRUPT_FILESYSTEM` | Consulted on-disk structures failed consistency checks. |

The exact last inspector result remains in `provider.lastDiskStatus`; exact
transport and cleanup results remain separate. Invalid arguments and policy
refusals do not invent a media diagnosis.

All twelve provider callbacks have the established VFS types. Mutation callbacks
always return `READ_ONLY`, including for a deliberately overprivileged trusted
Kernel client created outside the mount helper. The current VFS has no provider
rights callback for writable open itself. Consequently the mount helper enforces
read-only open grants; the provider independently enforces mutation refusal.

## Persistent console commands

| Command | Behaviour |
|---|---|
| `disks` | Existing transport report; find the actual modern block-device slot. |
| `mountdisk SLOT PART` | Open a checked primary FAT16 partition and attach the console's read-only client. Partition slots are zero-based. |
| `mountinfo` | Report mount state, slot, primary-partition slot and attached client count. |
| `diskls PATH` | List one complete bounded directory through the mounted VFS. |
| `diskcat PATH` | Read and display a complete file no larger than 4,096 bytes. |
| `unmountdisk` | Close the console descriptor and client, then close the mount; repeat to retry retained cleanup. |

The mounted disk is a separate VFS domain rooted at `/`. The ordinary `ls`,
`cat`, `write` and other established file commands continue to use their RAMFS
domain. No `/disk` mount crossing or user-program namespace replacement is
introduced. The earlier `partitions`, `fatinfo`, `fatls` and `fatcat` inspection
commands remain available when their transport is not held by this mount.

`diskls` stages up to the inspector's directory-entry limit before output.
`diskcat` stages the complete allowed file. Both close their temporary descriptor
before publishing terminal content. Control bytes are escaped so a binary file
cannot inject terminal control sequences. Files exceeding the bound return a
limit status without displaying a misleading prefix. Poweroff calls the same
explicit cleanup path before the existing shell shutdown sequence continues.

Repeated `mountdisk` cannot replace or close a live mount. Refused platform
discovery and failed pre-admission attempts may be retried after cleanup. One
console cannot take over another console's owner.

## Boot compatibility required by real guest qualification

The established ELF entry remains `_start` at `0x80200000`. A separate
eight-byte RV64 Assembly bridge at `0x80000000` forwards QEMU's machine-mode
firmware entry to it while preserving the incoming hart ID and DTB address.
The bridge occupies a page within the low-RAM range already reserved by boot
memory ownership. It does not move Kernel payload sections, change allocator
geometry, or alter separately linked user-program entry points.

The QEMU 8.2.2 build used for qualification supplies nonzero property alignment
padding in its generated DTB. The existing strict parser correctly rejects that
encoding under DTSpec section 5.4.1. Boot capture therefore uses a bounded owned
copy that canonicalises only those property padding bytes and then invokes the
unchanged strict reader. Node-name padding and all structural, bounds, overlap,
name and value checks remain enforced. The original firmware bytes are never
modified. The compatibility path reports its padding corrections and clears
its scratch copy after the catalogue owns the required observations.

These compatibility additions were required to execute the pre-existing guest
acceptance path on the available emulator. Neither native filesystem tests nor
an independently injected test DTB would alone qualify the normal boot path.

Primary references:

- [QEMU RISC-V firmware options](https://www.qemu.org/docs/master/system/target-riscv.html)
- [QEMU RISC-V virtual platform](https://www.qemu.org/docs/master/system/riscv/virt.html)
- [DTSpec flattened format, section 5.4.1](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html#lexical-structure)

## Deliberate remaining boundaries

The accepted disk format remains the previous inspector's checked primary MBR
and FAT16 subset, with bounded 8.3 aliases and directory/file work. This addition
does not introduce FAT writes, repair, formatting, GPT, extended partitions,
FAT12, FAT32, long filenames, mount-point traversal or a general device manager.
It does not automatically mount a disk, route disk access into user processes,
add asynchronous I/O or make a single-hart driver safe on multiple harts.

Use the existing disposable synthetic disk for qualification. It has known root
and nested files, a fragmented file and deliberately distinct slack bytes. The
source fixture and its CMake hash check are unchanged. Detailed build, test and
Windows merge instructions accompany the validation guide and delivery review.
