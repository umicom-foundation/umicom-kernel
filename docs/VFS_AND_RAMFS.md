# Umicom Kernel — Virtual filesystem and RAM-backed files

## What a file adds to the Kernel

Until now, the native loader has received an executable as an immutable byte
buffer. A filesystem gives those bytes a name, a length and an explicit lifetime.
It also lets services store ordinary records without making each service invent
its own page lists and pathname rules.

Consider `/bank/accounts/journal`. The name identifies a file in a directory;
it is not the file's physical address. Opening that name creates an open
description with a current position. A client receives a descriptor carrying
rights to use that description. Those distinctions matter when a name is removed
while a reader is still using the old file.

This implementation provides the Kernel-side foundation. It does not yet expose
file syscalls to user programs, attach descriptors to scheduled processes, or
start an interactive shell. It does not touch a disk, host directory or network.
All file data is lost when the machine restarts.

## Responsibilities stay separate

```text
Kernel-selected client and descriptor rights
                   |
                   v
VFS: bounded paths, typed operations, open descriptions and descriptors
                   |
                   v
RAMFS: directory links, node pins and file byte ownership
           |                          |
           v                          v
Existing object cache          Existing physical allocator
metadata node records          independently allocated file pages
```

`UmicomKernelVfsOperations` is the provider contract. The VFS invokes operations
such as lookup, pin, read and write; it does not inspect RAMFS page lists. This
batch mounts one provider at `/`. The contract leaves room for another backend,
but mount crossing, an arbitrary mount table and storage drivers are not implied.

The root node is embedded in the RAMFS owner. Other nodes use the existing object
cache, and file pages use the existing physical allocator. No second allocator,
page walker or executable parser is introduced. Existing services retain their
original storage and interfaces.

## Four different lifetimes

A **name** is a directory link. Removing it makes a subsequent lookup fail.

A **node** owns metadata and file pages. Its monotonic identity is local to the
provider. Reusing a record slot does not reuse an old node identity.

An **open description** pins a node and owns its current byte position, append
mode and directory cursor. Two separate Open calls receive independent
positions. A duplicate descriptor instead shares the existing description.

A **descriptor** is one client-local reference with a generation and rights.
Closing one duplicate does not close its shared description while another
reference exists. The last close releases the node pin.

This is why the following sequence is valid:

```text
open journal -> remove journal's name -> read through the old descriptor
                  |
                  +-> a new journal can be created under the same name
```

The new file has a new identity. It does not steal the old file's pages or
replace what an existing reader sees. Reaping the old node becomes possible
only after the old descriptions have released their pins.

## Mount and client lifetime

Allocate `UmicomKernelRamfs`, `UmicomKernelVfs` and each
`UmicomKernelVfsClient` in stable, fully zero-filled Kernel storage. Do not copy,
reset or reinitialise a live or closed owner. Resetting generations would revive
stale identifiers; copying an owner would leave borrowed pointers aimed at the
original object.

The normal order is:

```text
RamfsInitialize -> VfsMount -> VfsClientOpen -> file operations
                                                  |
                              VfsClientClose <----+
                                      |
                                  VfsUnmount
                                      |
                                   RamfsClose
```

Mount pins the root. A mounted backend therefore cannot close, even if it has
no files. Unmount refuses while a client is attached, including a client with no
open descriptors. ClientClose releases its descriptors and only then detaches.
If a close fails, its output count reports completed closes and the remaining
ownership is available for retry.

The client principal is chosen by trusted Kernel admission. It is not derived
from a filename or accepted from a user register. This batch does not yet bind
clients to process-supervisor records or automatically close them on a process
exit. That requires an explicit lifecycle integration, not a borrowed pointer
added to an unrelated service.

## Rights are deliberately small

| Right | Meaning |
|---|---|
| READ | Copy file bytes out through this descriptor. |
| WRITE | Copy bytes in and resize the file. |
| QUERY | Read a value snapshot of node identity, type and size. |
| ENUMERATE | Read directory-entry snapshots and rewind a directory cursor. |
| DUPLICATE | Issue another descriptor with the same or fewer rights. |
| CREATE | Add names in this mounted namespace. |
| REMOVE | Remove names in this mounted namespace. |

A client's rights are a ceiling for descriptors it opens. Namespace rights do
not become descriptor rights. A read-only observer cannot remove a journal just
because it has permission to read it. A read-only duplicate of a writable file
cannot write or manufacture a more powerful duplicate.

These are not per-file ACLs, credential-backed permissions, path capabilities
or a complete multiuser security policy. CREATE and REMOVE apply across the
whole mounted domain. Separate domains or narrower admission are needed when
that authority is inappropriate.

Descriptor tokens contain a 32-bit generation and an index plus one. Zero is
never issued. Closing a maximum-generation slot retires it instead of wrapping.
Tokens belong to one client; different clients can issue equal integers. The
trusted client selects the authority domain. A token alone is not a globally
unique or cryptographically authenticated capability.

## Path rules

Paths are canonical absolute printable-ASCII strings, up to 255 bytes excluding
the terminator. Each name contains 1–63 bytes, and there are at most sixteen
components. Spaces in names are allowed. The root is `/`.

These forms are refused rather than silently normalised:

```text
relative/file
/bank//journal
/bank/./journal
/bank/../journal
/bank/journal/
```

Backslashes, control bytes and non-ASCII bytes are also refused. There is no
working directory, symbolic link, hard link, rename or Unicode normalisation in
this implementation. Resolving the grammar first means an invalid later
component cannot cause an earlier partial namespace mutation.

Create is explicit and exclusive: it does not overwrite an existing file.
Open only opens an existing node; it does not create or truncate as a side effect.
The root cannot be removed. A directory must be empty and unpinned before its
name can be removed. A non-directory in the middle of a path is a typed error.

## Reading and writing

Read and Write return both a status and the actual byte count. After a validated
request, EOF is `OK` with zero bytes. A short EOF read is successful with its
actual prefix, not a fabricated full-size transfer.

New data pages are cleared before the first bytes are written. Pages need not
be adjacent. Missing pages represent zero-filled holes: seeking beyond EOF does
not allocate data, and a later write leaves its intervening gap readable as zero.

A write can exhaust the frame quota or allocator after it has copied a prefix.
That prefix remains committed, the size/position reflects only those bytes and
the error is returned alongside the count. The caller must retry the remaining
suffix, not assume that an error means nothing was written. Invalid or overflowing
ranges are rejected before copying; resource failure during a valid range is a
different case.

Append chooses the current file length at each Write. Duplicate descriptors share
append mode and position. These operations are serial and do not yield; this is
not a claim about concurrent append across harts or interrupt handlers.

Seek is an absolute byte position bounded by the backend's maximum file size.
It may move beyond EOF. Resize growth is lazy. Resize shrink clears truncated
bytes in every existing page so later growth cannot reveal old data. Shrink
retains allocated capacity until unlink/reap or domain closure; it does not
promise immediate physical-memory reclamation.

The API accepts trusted Kernel buffers. It does not validate arbitrary user
pointers. Buffers and output records must not overlap live filesystem ownership
storage. A future file syscall needs the existing checked-copy boundary and a
permitted execution context before it can invoke these operations safely.

## Directory enumeration is not a frozen snapshot

Enumeration returns names and node information as values, not borrowed node
pointers. Order follows the current record slots, not alphabetical order.

A structural change advances a filesystem-wide epoch. If a started cursor sees
another epoch, ReadDirectory returns `CHANGED` and retains its previous state.
RewindDirectory starts again explicitly. Even a change elsewhere in the tree
invalidates the cursor; that conservative policy avoids hidden missed entries.

File writes and resizing do not change namespace membership. Sizes in a returned
entry describe the node when that call executes; enumeration does not freeze
file contents. Duplicated directory descriptors share their cursor and epoch.

## Reclamation and failure

Unlink removes the name, not its memory. `UmicomKernelRamfsReap()` visits only
unlinked, unpinned nodes. It clears and releases each data page, then returns the
metadata object to its cache. Empty cache frames remain available for reuse and
are released when the filesystem closes.

If a physical release fails, its address and count remain recorded. The next
Reap or Close retries the incomplete teardown; already-released pages are not
freed twice. Close first refuses all pins, then moves into CLOSING before removing
names. Once closing begins, normal namespace and I/O calls cannot reopen it.

Validation resolves object-cache tickets before reading nodes, checks linked
parents and duplicate names/identities, and checks each data frame's allocator
classification before following its address. It also refuses aliases between
file data and other data/cache frames. Detected corruption poisons the owner;
it does not free memory using untrusted bookkeeping.

The physical `ALLOCATED` observation is still only a classification. It cannot
identify arbitrary malicious reuse by other machine-mode code. Exclusive
ownership, stable metadata and serial access remain necessary.

## Current limits

| Resource | Limit |
|---|---:|
| Nodes including the root | 64 |
| File size | 128 KiB per file |
| Data frames | 128 per filesystem, at 4 KiB each |
| Possible metadata-cache backing | Up to 16 independently allocated frames |
| Attached clients | 16 per VFS mount |
| Descriptors/open descriptions | 16 of each per client |
| Path length | 255 bytes |
| Name length | 63 bytes |
| Path depth | 16 components |

The data quota is 512 KiB of backing, plus separate metadata storage. Sparse file
lengths can exceed their allocated backing without exceeding the per-file limit.
The bounds are explicit development admission limits, not a desktop capacity
claim. Full validation scans favour clear ownership evidence over tuned speed.

## File bytes reach the existing loader

The guest acceptance sequence creates `/bin/umicom-diagnostic.elf`, writes the
independently linked diagnostic ELF through a descriptor and reads it back into
Kernel staging storage. The existing ProcessCreate validates and privately
loads those bytes. Then the test clears staging, closes both clients, unmounts
and destroys the entire RAM filesystem **before** running that process.

The existing runner receives identity 701 and argument 7. The unchanged program
must exit with value 741. The filesystem is not involved in executing it, and
there is no pointer left from the process into an open file buffer.

This proves the byte-source connection; it is not yet an `exec(path)` service,
a shell command, a loader with incremental file reads, or a persistent executable
store. The native test also mutates the stored ELF magic, confirms the real
loader refuses the bytes without allocating a process, repairs the file and
checks the successful image's copied and zero-filled bytes.

## Regular Windows workflow

Only the top-level CMake include and the final validation call in Kernel main
are additions to existing files. All existing lines and comments remain intact.
The usual incremental workflow is unchanged:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new guest test is `kernel.riscv64.vfs_ramfs`. It retains the current-image
fixture. A successful guest finishes with `UMICOM_KERNEL_VFS_RAMFS_READY` before
the existing end marker and test-finisher exit. The image still runs its tests
and powers off; it does not start a shell in this batch.

After successful tests, review the files and commit:

```powershell
git status
git add -A
git diff --cached --check
git diff --cached --stat
git commit -m "feat(kernel): add typed VFS and owned RAM-backed files"
git push origin main
```

## Optional native tests

The standalone host project does not modify the normal cross-build. On a host
with the native compiler and sanitizer runtime available:

```text
cmake -S tests/ramfs -B build/native-ramfs -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_VFS_SANITIZERS=ON
cmake --build build/native-ramfs --parallel 2
ctest --test-dir build/native-ramfs --output-on-failure --no-tests=error
```

That registers 65 policy tests. Supplying a previously cross-built diagnostic ELF
adds the real-byte loader bridge (not guest execution):

```text
cmake -S tests/ramfs -B build/native-ramfs -DUMICOM_VFS_TEST_EXECUTABLE=<absolute-path-to-programs/umicom-diagnostic.elf>
```

The placeholder must be replaced with an actual path. The option is deliberately
not set to an invented file by default; absent input does not count as a pass.

## Next integration boundary

The next work connects clients to scheduled-process lifetime, validates file
syscall buffers and builds bounded console input and shell commands. That work
must preserve the current descriptors' ownership rules and account for their
cleanup when a process stops. Persistent storage, credentials and permission
policy, mounts beyond `/`, and a normal boot path remain separate qualification
gates. None is silently enabled by this RAM-backed provider.
