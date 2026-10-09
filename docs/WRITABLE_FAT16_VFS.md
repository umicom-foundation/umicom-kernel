# Writable FAT16 through native file services

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

The writable disk mount exposes a bounded FAT16 namespace through the existing
VFS and native process file-service ABI. Applications can create files and
directories, read, write at a position, append, resize, enumerate and remove
closed objects. The mount uses one exclusive lifecycle lease. Each successful
mutation completes its own Stage and Finish before VFS reports success.

Read this alongside [architecture](ARCHITECTURE.md),
[process file services](PROCESS_FILE_SERVICES.md),
[file lifecycle](FAT16_FILE_LIFECYCLE.md),
[directory lifecycle](FAT16_DIRECTORY_LIFECYCLE.md) and
[ordered commits](FAT16_ORDERED_COMMITS.md). The existing immutable
[read-only mount](READ_ONLY_FILESYSTEM_PROVIDER.md) remains a separate interface.

## Ownership and the native API boundary

`UmicomKernelDiskWritableMount` owns the lifecycle committer, mutable provider
and mounted VFS. Its storage is initially zero-filled, stable and single-use.
`UmicomKernelDiskWritableMountOpen` takes an explicit block domain, device slot,
partition, finite timeout and Gregorian calendar. It checks the calendar before
acquiring transport ownership. No device or host path is selected automatically.
An admitted attempt may retain resources on failure; `Close` releases them or
retains the original owner for cleanup retry. Fresh storage is required for a
new admitted lifetime.

`UmicomKernelDiskWritableMountClientOpen` grants the chosen subset of VFS rights.
The VFS checks descriptor and namespace rights before calling the provider.
CREATE and REMOVE are authority over this entire mounted domain, without path
ACLs or authenticated credentials. A trusted Kernel process owner can attach
`mount.vfs` using `UmicomKernelUserFilesAttach` before task admission, and grant
each fresh READY task its chosen ceiling using `UmicomKernelUserFilesGrant`.
The existing copied request/result wire records and service number remain intact.

The process file path copies and validates user memory, returns from the trap,
verifies machine-state restoration, then invokes the VFS in the dispatcher.
This batch does not make blocking disk operations legal in an IRQ handler or
while user translation remains installed. The real guest qualification attaches
the mounted domain to a scheduler and executes native file ECALLs in U-mode.

The new provider and mount depend on Kernel mechanisms only. They do not import
Umicom Framework, a GUI, a Data Server or the portable userspace System Services
implementation. This is a native Kernel API, with distribution policy and
portable service adaptation still above it.

## Supported operations

| VFS operation | Persistent behaviour |
| --- | --- |
| Create file | Publish an empty short-name file; allocation begins at the first write. |
| Create directory | Initialise canonical dot and parent records, then publish the directory. Creation can grow an allocated parent within the lifecycle bounds. |
| Write | Overwrite existing bytes, or extend across EOF, with at most 4096 supplied bytes in one accepted commit. The starting offset must be at or before EOF. |
| Append | VFS observes the latest accepted size at each write. Separate descriptions therefore append in the caller's serial execution order. |
| Resize smaller | Publish the reduced size and reclaim the unused chain using lifecycle truncation. |
| Resize larger | Zero-fill at most 4096 additional bytes in one WRITE commit, including any stale slack exposed inside an existing cluster. |
| Resize to current length / zero-byte write | Local no-op with WRITE authority, a file without the FAT READ_ONLY attribute, and a usable owner; no timestamp change, WRITE, FLUSH or commit-count increment. |
| Remove | Remove a closed file or empty closed directory and reclaim its allocation. An open target returns BUSY. |
| Read / list | Query committed bytes through the same exclusive lease; never acquire a competing read-only block lease. |

The lifecycle API gains `UMICOM_FAT16_LIFECYCLE_WRITE` with a trailing request
`offset`. It preserves the earlier operation numbers. Existing operations ignore
that additional field. WRITE checks the 32-bit FAT file length, preserves every
unmodified byte in existing sectors, and initialises complete newly allocated
clusters. It reuses the whole-volume ownership proof and persistence sequence.
The earlier APPEND, TRUNCATE, directory and MOVE APIs retain their behaviour.

The maximum reported VFS file size is the smaller of the FAT16 32-bit limit and
256 clusters at the admitted cluster size. This is a format/profile bound, not a
promise of available free space. At most eight new clusters can be allocated by
one operation. Allocation, namespace, sector and total-read budgets can refuse
a request earlier with an explicit inspection or capacity result. A direct
lifecycle CREATE with 4096 initial bytes and parent growth can exceed the shared
eight-cluster allocation budget; the VFS creates an empty file and writes in a
separate operation. Large cluster sizes can reach the complete-sector staging
bound sooner. The API does not split one request into several hidden commits.

## Identity, descriptors and directory iteration

The mutable provider has 64 node records. Cache slots may be reused; issued node
IDs may not. Root and independent open descriptions pin their nodes. Duplicate
descriptors share position, append and iteration state, while separate opens have
independent positions. A successful write or resize updates the cached entry for
every description of that node without changing its identity.

Create publishes a durable name without returning or allocating a cache identity.
The later lookup or open has a separate cache admission and can return CAPACITY
when all records are pinned, or EXHAUSTED after the identity space ends. A
successful Create therefore does not reserve resources for a later Open.

Removing a pinned target returns BUSY before submitting mutation. This bounded
provider does not yet store persistent unnamed open files. Closing all target
descriptors permits removal; re-creating the same spelling receives a different
identity. The existing RAMFS unlink/open behaviour is unchanged.

Every accepted create or removal advances a nonzero mount-wide directory epoch.
An older enumeration returns CHANGED without publishing an entry or advancing
the cursor. Rewind starts a new iteration. The epoch conservatively invalidates
streams in other directories as well. File-data or size changes do not advance
it, and refused mutations do not invalidate iteration. Exhaustion is refused
before mutation rather than wrapping a generation.

The provider exclusively borrows its lifecycle owner. Calling lifecycle APIs
directly during that borrow, or changing the backing medium externally, violates
the ownership contract. An unexpected accepted-operation count or unusable
lifecycle state blocks further provider I/O. This is not media hot-swap support.

## What success and failure mean

Success requires the existing ordered dirty guards, data/FAT/directory writes,
FLUSH barriers, sector readback and accepted clean Finish. Caller input is
snapshotted before device callbacks. The provider publishes the new metadata and
reports the entire requested byte count only after acceptance.

A failed mutation reports zero **accepted VFS bytes**. Some media bytes may
already have changed, or even reached stable storage. The exact lifecycle result
retains submitted/completed sectors, phase, flush/verification evidence and
uncertainty. An unaccepted write must not be treated as an unchanged disk or
blindly retried. After media failure, the provider refuses further data access
and mutations for that lifetime, including no-op mutations.

Local stat returns the last accepted cached snapshot; it does not inspect the
result of a failed write. Local validation, unpin and descriptor close continue
to work. Close clients and process attachments, unmount the root, close the
provider, then close the lifecycle lease. Device reset or frame-release failure
retains CLOSING ownership for retry. Close never performs an implicit Finish.

This is ordered persistence with bounded interruption detection. It provides no
journal, rollback, automatic repair, power-loss atomicity, sector-atomicity claim,
multi-operation transaction, concurrent writer protocol or SMP lock. A late
failure can leave clean-looking bytes without an accepted acknowledgement;
the retained result distinguishes that case from success.

## Trusted console

Use an expendable copy of the synthetic fixture. Writable admission requires
VirtIO FLUSH support and a writable backend. Slot numbers come from `disks`.
The calendar below is explicit sample data; choose the convention appropriate
for the medium. FAT stores seconds at two-second resolution. The qualified
single-device QEMU command reports slot 7; use the slot reported by your `disks`.

```text
disks
mountdiskrw 7 0 2044-02-29T23:58:56
diskmkdir /WORK
diskcreate /WORK/NOTE.TXT
diskwrite /WORK/NOTE.TXT 0 "hello"
diskappend /WORK/NOTE.TXT " world"
diskresize /WORK/NOTE.TXT 16
diskrwcat /WORK/NOTE.TXT
diskrwls /WORK
diskrwinfo
unmountdiskrw
```

The final five bytes in that file are zero and are displayed as `\x00` by the
console. `diskrwtime YYYY-MM-DDTHH:MM:SS` changes the explicit calendar for later
mutations. `diskdelete PATH` removes a closed file; `diskrmdir PATH` removes an
empty closed directory. The console has one admitted writable mount lifetime
per boot, including failed admission. Start a fresh boot for another one.

The existing console input buffer is 512 bytes, including command syntax and
quoting. Its text payloads are therefore smaller than the native API's 4096-byte
transfer bound. `diskrwcat` displays a complete file of at most 4096 bytes; it
refuses larger files rather than streaming chunks through this command.

`mountdisk`/`diskls`/`diskcat` still belong to the immutable read-only mount.
`mountdiskrw`/`diskrwls`/`diskrwcat` use this mutable mount. Both require exclusive
device ownership, as do the older `fatfsopen` utilities; release one before
admitting another. The writable domain is rooted at `/` and is not a `/disk`
overlay in the console RAMFS. Ordinary `run` and `runrw` keep their RAMFS binding;
automatic executable loading or routing from a persistent disk is future work.

## Qualification

For a PowerShell test that drives the normal console, waits for each command
and verifies the same file after reboot, see
[Testing the writable disk console](WRITABLE_CONSOLE_TESTING.md).
It discovers the device slot and uses a fresh disposable fixture copy, so the
interactive test no longer depends on pasting into the serial prompt.

`tests/disk_writable_filesystem` links the production provider, mount, VFS,
lifecycle implementation, VirtIO driver and allocator to the established
independent visible/durable media model. It checks byte preservation, descriptor
rights and positions, cache identity and pin exhaustion, namespace epochs,
zero-extension, retained cleanup and console behaviour. Failure injection
includes I/O errors, discarded writes and cached/eager persistence interruptions.

The lifecycle suite adds separate positional-WRITE plan and transport cases,
including every observed read/write/flush failure point and both interruption
models. Existing tests stay registered. Native sanitizers are optional at CMake
configuration; their execution is separate evidence from QEMU.

`cmake/DiskWritableFilesystem.cmake` registers a native U-mode writer, a fresh
read-only boot and a writable-admission refusal on an immutable attachment.
The writer leaves `/WORK/LOG.BIN` with 900 verified bytes after create, overwrite,
append, zero-extension, truncation and allocation reuse. It also exercises
reduced task rights and open-descriptor cleanup before reap. An independent
whole-image oracle checks every disk byte, including unused directory bytes,
free-cluster contents and both FATs. CTest hashes the immutable fixture and
disposable images around their roles. Registered tests describe the intended
checks; the delivered engineering review records what actually ran.

## Remaining integration work

Move/rename remains available through the separate lifecycle API; VFS has no
rename operation in its current native wire contract. Persistent orphan files,
long filenames, sparse files, asynchronous I/O, mount crossing, authenticated
mount policy, boot from disk and recovery need their own implementations and
qualification. The native persistent-system roadmap gate remains open.
