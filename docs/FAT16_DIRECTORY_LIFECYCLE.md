# Creating and removing FAT16 directories

A directory is a collection of named entries. On FAT16, each ordinary directory
owns a chain of storage clusters, while the root directory occupies a fixed
reserved area. Umicom's trusted lifecycle owner can create a directory, create
children beneath it, grow an allocated parent when it runs out of entry slots,
and remove an empty directory.

These operations belong to the independent Kernel. They do not depend on
Umicom Framework, a GUI, or the Data Server.

## Work with a disposable image

Use the trusted console with a disposable copy of the synthetic FAT16 fixture.
A writable VirtIO device must support flushes. Do not use a personal disk as a
test fixture.

1. Identify the intended device with `disks`. Open its partition with
   `fatfsopen SLOT PARTITION`, substituting the device and partition numbers.
2. Supply the calendar explicitly:

   ```text
   fatfstime 2044-02-29T23:58:57
   ```

   FAT stores modification times in two-second units. This example is stored as
   `23:58:56`. Umicom does not invent a wall-clock timestamp.
3. Create the parent and finish that operation before creating a child:

   ```text
   fatmkdir /WORK
   fatfscommit
   fatmkdir /WORK/NOTES
   fatfscommit
   ```

   Missing ancestors are reported as `not-found`; `fatmkdir` does not implicitly
   create a chain of missing parents.
4. Create and append a file inside the directory:

   ```text
   fatcreate /WORK/NOTES/HELLO.TXT "hello"
   fatfscommit
   fatappend /WORK/NOTES/HELLO.TXT " world"
   fatfscommit
   ```

5. Remove the file before removing its containing directories:

   ```text
   fatdelete /WORK/NOTES/HELLO.TXT
   fatfscommit
   fatrmdir /WORK/NOTES
   fatfscommit
   fatrmdir /WORK
   fatfscommit
   fatfsclose
   ```

   `fatrmdir` requires an empty directory. It does not recursively delete
   children. Removal needs no new calendar because it does not create or
   update a surviving entry's timestamp.

Use `fatfsinfo` to inspect the latest operation's evidence. Stop on an error.
A refused operation that submitted no writes leaves the session available for
another valid request. Once a write has been submitted and the operation fails,
the owner becomes unusable for further mutation; close it and retain the image
for inspection.

## Inspect the committed contents without closing the session

After each successful `fatfscommit`, use the same exclusive lease to inspect
its committed data:

```text
fatfsls /WORK
fatfsstat /WORK/NOTES/HELLO.TXT
fatfsread /WORK/NOTES/HELLO.TXT 0 64
```

`fatfsls` lists the selected directory. `fatfsstat` shows the persisted size,
cluster and modification time. `fatfsread` reads a byte range: the last two
arguments are the byte offset and maximum number of bytes, from 1 to 4096.
Reading beyond the end returns zero bytes. The console displays binary control
bytes as dots so file content cannot send terminal commands.

These queries submit no writes or flushes. They freshly inspect the disk under
the already-held device lease and preserve the previous mutation result and
accepted-operation count. A query while a change is staged reports `bad-state`;
finish that change before browsing. Failed or closed mutation owners cannot
inspect through this interface. A missing path or a failed read leaves the API
caller's previous result unchanged.

The separate `fatls`, `fatcat` and `fatstat` commands remain read-only-device
inspectors. For an independent reboot check with those commands, attach the
saved disposable image with `readonly=on`, run `disks` to discover its slot,
and use that slot with partition zero. A writable attachment is deliberately
refused by those separate inspectors; it is not an empty filesystem.

## What directory creation writes

The planner first proves the complete bounded namespace and allocation map. It
checks both FAT copies, detects shared or orphan allocation, and selects free
clusters in increasing identifier order.

A new directory receives one completely initialized cluster. Its first two
records are `.` and `..`:

- `.` references the new directory's own first cluster.
- `..` references its parent's first cluster, or zero for the fixed FAT16 root.
- Both records have the directory attribute, zero file size, and the same
  supplied creation, access and modification date fields as the parent entry.

The remaining bytes are zeroed, so previously unused space cannot expose old
directory entries. Short names follow the existing absolute 8.3 path rules.
Names that differ only in ASCII letter case identify the same entry.

## Growing an allocated parent

When all existing entry slots are occupied, file creation and directory creation
can add one cluster to an allocated parent. Deleted slots are reused first.
Growth never changes the fixed root into an allocated directory.

The new cluster is initialized with the new entry before the old tail is linked
to it. A directory created during this operation receives a different cluster
from the parent extension. The planner explicitly excludes allocations already
chosen for the child.

The new entry's data image and directory image are identical. After the FAT
links are persisted, the owner verifies that image rather than submitting a
second identical write. Directory publication counters therefore describe
actual metadata submissions; initialization remains in the data-sector counters.
A zero payload count does not mean that no initialization sectors were written.

Removing an empty directory reclaims its complete allocation chain. Deleting its
last child does not automatically shrink or remove its parent.

## Bounds and refusals

<!-- The earlier shared-plan wording is retained for review. Move support now
needs four metadata sectors; creation and removal still use at most two.
operation, 72 data-sector images, 32 FAT-sector images, two directory-sector
images, chains of at most 256 clusters, 64 directories, 256 namespace objects
-->

The existing profile bounds remain explicit: eight newly allocated clusters per
operation, 72 data-sector images and 32 FAT-sector images. Creation and removal
use at most two directory-sector images; the shared plan has room for four to
support [file and directory moves](FAT16_FILE_MOVES.md). Other bounds include
chains of at most 256 clusters, 64 directories, 256 namespace objects
at most 512 raw entries in each allocated directory, and the existing total
read budget. The parent extension counts against the
same allocation and initialization budgets as the child.

Consequently, a request can be valid in FAT16 yet exceed this Kernel profile.
For example, a file that consumes all eight permitted new clusters cannot also
grow its parent in the same operation. A new directory whose single cluster
would contain more than 512 raw entries is also refused, so successful creation
cannot publish a directory that the next reader cannot inspect. `limit` reports a profile bound;
`no-space` reports unavailable allocation or fixed-root slots.

Root removal, removal of a regular file through `fatrmdir`, nonempty directories,
read-only target directories, malformed dot entries and invalid requests are
refused. Read-only parent directories refuse creation. Existing file APIs retain
their established behavior.

## Stage, Finish and interruptions

Stage persists dirty markers before data, allocation or namespace changes.
Creation initializes storage before publishing its allocation. Removal persists
the parent tombstone before freeing the removed directory's clusters. Finish
rechecks the planned images before publishing clean FAT headers.

Stage alone is not an accepted commit. Closing an unfinished owner releases its
resources without finishing, rolling back or repairing the image. A fresh
ordinary inspector refuses the resulting dirty filesystem.

The test suites include injected reads, writes and flush failures; altered or
dropped writes; cached and eager interruption models; and separate QEMU writer
and reader boots. The guest checks compare complete disposable images, including
unrelated files and retained freed bytes. Controlled omission of Finish is not a
physical power cut. This protocol does not provide journalling, automatic
recovery or power-loss atomicity.

## Extending the implementation

Keep directory policy in the lifecycle planner and device ownership in the
lifecycle committer. A new operation must validate its complete request before
callbacks, snapshot borrowed input, preserve caller outputs on refusal and prove
allocation ownership before submitting writes.

Extend the independent media oracle as well as the request and failure tests.
Do not make an expected image by copying the production plan: that would allow
the test and implementation to repeat the same mistake. Keep initialization,
allocation publication and accepted completion distinct in both code and
diagnostics.
