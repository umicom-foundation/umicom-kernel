# Existing FAT16 data updates

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and place in the storage roadmap

The Kernel can now prepare and submit a bounded update to the contents of an
existing FAT16 file, explicitly flush the writable block lease, and inspect the
result through the existing read-only filesystem after reopening the medium.
This extends the writable VirtIO transport into checked file-offset operations.

The operation preserves all filesystem metadata. File size, cluster allocation,
names, directory entries, timestamps, attributes and FAT flags remain unchanged.
It is a trusted data-update utility, with a separate lifetime from the read-only
VFS mount. It does not yet implement a general writable FAT filesystem or the
complete persistent-system milestone in the release roadmap.

The distinction is concrete. Microsoft's FAT specification requires a general
filesystem writer to maintain modification timestamps and the archive attribute.
This utility requires ARCHIVE already set and deliberately preserves timestamps.
It also leaves the existing clean/error flags unchanged; these flags cannot
record this utility's uncertain data writes. A later metadata protocol must
address those requirements before a writable mount is introduced. See the
[Microsoft-authored FAT specification, sections 4.2 and 6.2–6.3](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf).

The work remains native Kernel C23 code. It introduces no dependency on the
Umicom Framework, Studio, trading systems or an OS distribution. The broader
GNU Linux-libre production direction and native Kernel research direction keep
their existing roles in `KERNEL_AND_OS_RELEASE_ROADMAP.md`.

## Why the existing VFS is kept read-only

The current VFS write callback returns a transferred byte count. Its callers can
advance a file position using that exact count. A published disk write can fail
after changing an unknown part of a sector, which cannot honestly be described
as a simple retryable byte prefix. The existing callback also has no FLUSH
operation.

The FAT16 provider additionally promises an immutable medium from provider Open
through Close. Updating its backing bytes while the same provider remains open
would break that promise. The new updater therefore owns its own writable block
lease and opens an inspector only during read-only preparation. That inspector
closes before the first WRITE. The existing provider and VFS grant boundaries
remain intact.

The qualified block domain prevents simultaneous local ownership of the same
slot. The caller must also provide exclusive backing-medium ownership: a local
lease cannot stop a host program or another machine from editing the backing
file. All API pointers and callbacks remain trusted Kernel inputs.

## Source organisation

| Files | Responsibility |
| --- | --- |
| `include/umicom/kernel/fat16_update_plan.h`, appended code in `kernel/fat16_inspector.c` | Read-only whole-volume qualification and private patched-sector plan |
| `include/umicom/kernel/fat16_update.h`, `kernel/fat16_update.c` | Writable lease, operation deadline, mutation evidence, explicit flush and retained cleanup |
| `include/umicom/kernel/fat16_update_console.h`, `kernel/fat16_update_console.c` | Five explicit trusted-console commands and historical result reporting |
| `kernel/fat16_update_validation.c`, `kernel/fat16_update_boot.c` | Dedicated writer and fresh read-only guest acceptance |
| `cmake/Fat16Update.cmake` | Image registration and disposable-copy fixture dependencies |
| `tests/fat16_update/` | Native format, lifetime, failure and persistence-model qualification; deterministic guest fixture checks |

`UMICOM_DISK_READ_ONLY` is appended to the disk status enum. Every older numeric
status retains its value. The original last enum line remains in an explained
disabled block, and the original inspection implementations remain present.
All Kernel consumers must rebuild together after public-header changes.

## The bounded update profile

| Quantity or feature | Accepted scope |
| --- | --- |
| Input per operation | 1–4096 bytes |
| Byte range | Wholly within the existing file size; no implicit extension |
| Physical request | One complete 512-byte sector at a time |
| Touched sectors | At most nine, accounting for unaligned 4096-byte input |
| Name lookup | Absolute portable short-name paths, case-insensitive 8.3 aliases |
| Path storage | 256 bytes including the terminating NUL |
| Path depth | At most eight components |
| File or directory chain | At most 256 clusters, ending at a checked EOC |
| Directory records examined | At most 512 allocated records per directory |
| Published ordinary entries | At most 128 per directory |
| Whole reachable namespace | At most 256 live objects, including root |
| Directory queue | At most 64 directories, including root |
| FAT copies | Exactly two, with every declared sector compared |
| FAT size | At most 256 sectors per copy |
| Cluster ownership bitmap | 8192 bytes covering every 16-bit cluster number |
| Reader calls | At most 4096 in the enclosing updater operation |
| Enclosing acceptance deadline | 100,000,000 clock ticks, restarted for each operation |
| Target attributes | READ_ONLY clear and ARCHIVE already set |
| Live long-name records | Refused by this update profile |
| Bad, reserved, malformed or orphan allocation | Refused |

The existing inspector's geometry rules still apply: supported primary MBR FAT16
partition type, 512-byte sectors, bounded power-of-two sectors per cluster,
consistent BPB/partition sizes, valid FAT16 cluster count, agreeing FAT headers,
and both clean/no-error bits set. The updated strict path does not make the
original read-only inspector more restrictive.

A bound is an admission rule, not a partially successful traversal. Exceeding
one refuses the update before any WRITE. A valid FAT volume outside this small
profile may still be readable by another implementation.

## Proving that the file owns its allocation

Checking the requested file's chain alone is insufficient. A different file can
point into that chain, or a directory can use the same cluster. Overwriting the
target would then change another object's data or directory records.

Preparation uses the original checked parser functions and adds a complete,
bounded ownership walk:

1. Look up the requested file, check its attributes and complete byte range,
   and validate its entire chain against its existing size.
2. Walk the root and every reachable child directory using a fixed queue.
   Validate each ordinary object's complete chain. Mark each cluster in the
   ownership bitmap once; a repeated mark refuses the volume.
3. Check records the read-only directory view intentionally skips. Non-root
   directories must start with canonical `.` and `..` entries linking to the
   actual containing and parent directories. A volume label must have no
   allocation, must occur only in the root, and may occur only once. Live LFN
   records are refused. Deleted records do not own live allocation.
4. Compare both complete FAT copies and classify every usable data-cluster
   entry. Every allocated cluster must have exactly one reachable owner and
   every free cluster must be unowned. An allocated but unreachable predecessor
   pointing into the target is therefore refused too.
5. Map the requested logical byte offsets to checked data-sector LBAs, read
   every touched sector, and patch only the requested byte spans in private
   storage. Publish the complete plan only if every preceding check succeeds.

The allocation scan stops interpreting FAT entries at the last data cluster.
It compares the copies' remaining padding bytes without assuming those bytes
represent additional clusters or must all be zero. An end-of-directory marker
ends the live namespace; stale entries after it are not live owners, and any
allocation left solely by them is caught by the orphan check.

The planner copies its input before its first media callback. The enclosing
updater performs initial inspector-open reads before calling the planner, so
the caller must keep input stable throughout the complete Write call. There is
no claim of an early snapshot before those initial reads.

## Submission, completion and flush

The updater closes its temporary inspector and writes each fully staged sector
in logical file order. No later pre-read can fail after a first write because
all such reads have already completed. This does not make the sequence atomic:
a later WRITE can still fail after earlier sectors changed.

The result records both requested caller bytes and device-sector observations:

| Result field | Meaning |
| --- | --- |
| `confirmedBytes` | Caller-byte prefix covered by fully validated successful sector completions |
| `submittedBytes` | Caller bytes in all published sector requests, including a following uncertain request |
| `completedSectors` / `submittedSectors` | Corresponding complete-sector counts |
| `uncertainOffset` / `uncertainBytes` | The requested-file-byte intersection of a published but unconfirmed sector |
| `uncertainSector` | Absolute LBA of the entire 512-byte sector potentially damaged |
| `lastBlockOutcome` | Observation for the last attempted block mutation |
| `diskStatus` / `blockStatus` | Filesystem and transport diagnostics retained alongside the aggregate status |
| `needsFlush` / `writeUncertain` | Lease flags as observed when this result was returned |

An uncertain sector can include neighbouring file bytes or allocation slack
outside the requested range. The shorter caller intersection is not a promise
that those other bytes are intact.

For the guest example, file offset 511 and length 700 touch three sectors. Their
caller-byte contributions are 1, 512 and 187 bytes:

| Observation | Confirmed | Submitted | Uncertain caller span | Aggregate outcome |
| --- | --- | --- | --- | --- |
| Final pre-read fails | 0 | 0 | None | NOT_SUBMITTED |
| First sector completes; next request is refused locally | 1 | 1 | None | PARTIAL_CONFIRMED |
| First sector completes; second is published without validated success | 1 | 513 | File offset 512, length 512 | SUBMITTED_UNCONFIRMED |
| Every sector completes successfully | 700 | 700 | None | COMPLETED |

After any unconfirmed WRITE, further Write calls on that owner are refused.
The original result remains available. Flush and Close can still be requested;
the transport's own faulted state may prevent further I/O while still retaining
the cleanup obligation. There are no implicit retries.

`COMPLETED` on a Write is a device completion observation. A later successful
FLUSH is required to clear `needsFlush`; persistence also depends on the backend
honouring its acknowledgement. A successful FLUSH never clears `writeUncertain`
or changes the earlier `lastWrite` record. This follows the separate write and
flush operations in the [VirtIO block specification, section 5.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

A whole-operation deadline check after the last sector can fail even when all
individual block completions succeeded. In that case the status reports the
deadline error and the result retains the completed counts. A non-OK return
never implies rollback or identifies a safe retry point by itself.

## Ownership and cleanup

The public owner is stable, initially zero-filled Kernel storage. It contains
the temporary inspector, fixed planning workspace, private sector plan and
diagnostics. The input, terminated path and aligned output result must be
pairwise disjoint and independent of that owner, the block domain and every
retained DMA frame in the domain. Invalid extents and address overflow are
refused. These checks do not probe arbitrary user memory.

The updater sets its reentry guard before calling even the execution-policy
callback. Typed ownership, storage, state, busy, unsafe-context and prior-write-
uncertainty refusals leave caller results and previous write evidence untouched.
After admission, a complete result is returned even for a filesystem preflight
refusal. Planning scratch and patched sector bytes are scrubbed after use.

Open acquires a separate FLUSH-capable writable lease and checks the initial
filesystem geometry. Every Write repeats the full preparation. A failed Open
can leave a real handle requiring Close; storage cannot be reused until that
cleanup succeeds. A successfully admitted lifetime is single-use, matching the
existing stable-owner approach. Failed admission may be retried after complete
cleanup.

Close retires any temporary interpretation, resets the device and releases DMA
frames through the existing transport. A reset or release failure retains the
handle and CLOSING state so Close can be retried. Close never flushes, repairs
metadata, retries data, clears uncertainty or proves persistence. Closed owners
retain their result evidence, and repeated completed Close calls are harmless.

## Trusted console commands

| Command | Effect |
| --- | --- |
| `fatwriteopen SLOT PART` | Acquire the explicit updater lease for a selected block slot and primary partition |
| `fatwrite PATH OFFSET "TEXT"` | Update the existing file range with the parsed nonempty text bytes |
| `fatflush` | Explicitly request and report FLUSH |
| `fatwriteinfo` | Show current lease flags and the last admitted write result |
| `fatwriteclose` | Release resources while preserving mutation evidence |

The four-token parser already supports the write command. The console text
length is bounded by its existing line buffer; the Kernel API accepts the full
4096-byte maximum. Ordinary RAMFS `write` and the read-only `mountdisk`, `diskls`
and `diskcat` commands keep their existing meanings.

The console owner is bound to the originating shell. Teardown includes its
retryable cleanup. A refused command labels previous write evidence as
historical; a later flush does not make old result snapshots appear current.
Poweroff does not submit a hidden flush. Explicitly flush successful intended
updates before closing or shutting down.

## Persistence qualification and practical limits

The dedicated writer receives an 8 MiB disposable copy of the unchanged FAT16
fixture. It verifies the original bytes, updates `/FRAG.BIN` at offset 511 for
700 bytes, compares the complete medium, explicitly flushes and releases its resources.
A separate QEMU process opens that same copy read-only and verifies it again,
including reads through the existing VFS provider. Whole-medium comparison
checks metadata, untouched file bytes, other files, slack and unused sectors.

The writer uses actual writeback caching and normal flush behaviour. Snapshot
mode is unsuitable for this test because QEMU documents its use of unsafe
caching. The automatic test therefore uses neither `snapshot=on` nor `-snapshot`.
See [QEMU block-device cache options](https://www.qemu.org/docs/master/system/invocation.html).

The native model keeps visible and durable images separately. Injected partial
writes, failed flushes and simulated loss of unflushed state qualify the API's
observations. They do not establish physical power-loss safety or sector
atomicity on real hardware. The guest process restart demonstrates the tested
file-backed persistence path, not a power cut to the host storage device.

No create, truncate, extend, rename, remove, allocation change, timestamp update,
dirty/error marker protocol, journal, repair, checksum or crash-recovery scheme
is provided here. The owner remembers uncertainty only in RAM; restarting the
machine loses that record while unchanged FAT flags cannot reconstruct it.
Read-only filesystem admission can prove structural checks, not that earlier
user-data bytes are correct after an interrupted update.

The next storage design needs explicit metadata ordering and persistent failure
state before writable filesystem operations are exposed through ordinary VFS
descriptors. Recovery and power-loss acceptance remain part of that work.

Build instructions, exact executed results, fixture hashes and integration
commands accompany this document in `FAT16_UPDATE_VALIDATION.md` and the
self-contained HTML delivery review.
