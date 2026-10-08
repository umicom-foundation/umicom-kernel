# Moving FAT16 files and directories

A move changes where a named entry is found. A rename is a move within the same
parent directory. The Kernel's lifecycle owner supports both for regular files
and directories, including directories that contain other files.

The operation keeps the existing data, allocation chain, attributes and stored
timestamps. Moving a directory between parents updates its `..` entry to point
to the new parent. The fixed FAT16 root is represented by parent cluster zero.

## Try a move in the trusted console

Use a disposable FAT16 image with the normal system, and discover its device
slot with `disks`. This example assumes you have already created
`/WORK/NOTES/HELLO.TXT` using the [directory guide](FAT16_DIRECTORY_LIFECYCLE.md).

1. Open the intended device and partition with `fatfsopen SLOT PARTITION`.
   Substitute the numbers reported for your disposable device.
2. Rename the containing directory and explicitly finish the operation:

   `fatmove /WORK /PROJECT`

   `fatfscommit`

   The file is now `/PROJECT/NOTES/HELLO.TXT`. The original `/WORK` path is absent.
3. Move the nested directory to the root:

   `fatmove /PROJECT/NOTES /NOTES`

   `fatfscommit`

   Its file is now `/NOTES/HELLO.TXT`. The parent reference stored inside `NOTES`
   now names the fixed root.
4. Inspect the saved result through the existing lease:

   `fatfsls /NOTES`

   `fatfsstat /NOTES/HELLO.TXT`

   `fatfsread /NOTES/HELLO.TXT 0 64`

5. Close the session with `fatfsclose`. For an independent reboot check, attach
   the same disposable image read-only, use `disks` again, then run
   `fatls SLOT PARTITION /NOTES` and
   `fatcat SLOT PARTITION /NOTES/HELLO.TXT`.

Every move needs its own successful `fatfscommit`. Moving needs no new
`fatfstime`: it preserves the calendar already stored on disk. Browsing while
a change is staged reports `bad-state`; finish the change first.

Stop when a command reports an error. `fatfsinfo` describes the most recent
operation. A refusal before writes leaves the session available for a corrected
request. After a submitted write fails, only closing the owner is permitted.
Closing never repairs, retries or silently accepts an unfinished move.

## What the API accepts

Use `UMICOM_FAT16_LIFECYCLE_MOVE` with `UmicomKernelFat16LifecycleStage`, followed
by `UmicomKernelFat16LifecycleFinish` on the same exclusive owner.

Supply two independent absolute short-name strings: `path` is the existing
source and `destination` is the complete new path, including the new entry name.
Use zero-initialized request storage and named fields. Leave `input`, `bytes`,
`size` and every `time` field zero. Other lifecycle operations ignore
`destination`; their data and calendar requirements remain unchanged.

The owner checks both complete strings against protected owner, result, request
and transport storage before copying them. The read-only planner independently
enforces its own storage boundaries. These are trusted Kernel addresses, not a
user-memory probing interface.

The source and both parents must permit the namespace change. There is no
overwrite option: an existing case-insensitive destination returns `exists`,
including a same-name rename supplied in independent string storage. Missing
parents are not created implicitly.

A same-parent rename reuses the existing entry, so exhausted physical directory
slots do not prevent it. A move to another parent needs an existing free or
deleted entry. It returns `no-space` when that parent is full; this metadata-only
operation does not allocate an extension. File and directory creation retain
their separate bounded parent-growth support.

The fixed root itself cannot move. A directory cannot move inside itself or one
of its descendants. The planner checks the resulting depth of every directory
in the moved subtree, including whether a deepest directory still contains
children. It refuses a result that the bounded reader could not inspect.

## What is written and why

Before preparation, the planner proves the complete bounded namespace, its
unique allocation ownership and both FAT copies. It then snapshots up to four
complete metadata sectors. It never receives a caller-selected physical sector.

A same-parent rename changes only the entry's eleven short-name bytes and
clears its two supported lowercase-display flags. All other entry bytes remain
unchanged, including raw creation tenths, access date and archive attribute.

A move between parents copies that entry into the destination slot and marks
the old entry as deleted. For directories, only the cluster field of `..`
changes inside the directory. No payload sector or FAT allocation link changes.
Previously deleted bytes remain present; this is not secure erasure.

Stage first persists and verifies dirty flags in both FAT headers. It then
writes and verifies the moved directory's parent reference, the old entry's
tombstone, any required successor end marker, and finally the destination.
Each distinct sector has its own flush and verification. This order ensures
that the destination is not exposed before the preceding metadata changes have
been checked.

Finish rechecks every planned sector and both dirty headers before publishing
clean headers. Successful completion increases the owner's accepted-operation
count. The result separately records submitted directory sectors and confirms
that no data payload or allocation links were written.

This is not journalling, rollback, automatic recovery or power-loss atomicity.
An interrupted move can leave missing or inconsistent names on a dirty volume.
A fresh ordinary inspector refuses that dirty volume rather than attempting to
repair it. A late error can also follow a clean publication; historical result
flags do not authorize a retry.

## Maintaining the implementation

Keep namespace decisions in the read-only planner and device ordering in the
exclusive lifecycle owner. Extend the independent whole-image oracle whenever
the permitted byte changes expand. Do not derive expected bytes from the
production plan.

Native checks cover empty and nonempty files, directory subtrees, root moves,
full-parent rename, destination boundaries, pointer overlap, depth limits and
all observed I/O failure positions. Dedicated RV64 writer, fresh reader,
interrupted-writer and rejecting-reader roles check complete disposable images
and allocation accounting. Controlled omission of Finish is a protocol
interruption test; it does not simulate every physical power-loss behavior.

The separate regular-file `fatrenamestage` API keeps its original same-parent
contract. `fatmove` adds the broader operation to the reusable lifecycle session.
