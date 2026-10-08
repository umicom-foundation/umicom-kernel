# Persistent FAT16 regular-file lifecycle

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and release boundary

The trusted Kernel can create a regular file, append while allocating new
clusters, truncate while releasing allocation, and delete a regular file.
An exclusive lifecycle owner can complete several independent operations
under one live lease. Every operation has a separate Stage and explicit
Finish, and the next operation starts from a fresh clean inspection.

This advances the persistent save/reboot/read direction in
`KERNEL_AND_OS_RELEASE_ROADMAP.md`. Creation, growth and allocation reuse now
have one consistent transport contract. Existing overwrite, slack-only append
and rename owners retain their established APIs. The Kernel remains
independent of the Framework, GUI and Data Server. Native research remains
separate from the recorded GNU Linux-libre production direction.

The new interface is a bounded trusted-console and Kernel primitive. It does
not expose writable disk VFS rights, create or extend directories, implement
long names, replace an existing file, move files between directories, provide
sparse files or repair damaged media. Dirty-header detection and ordered
publication do not provide a journal, transaction rollback or atomicity across
power loss. This work alone does not establish a supported persistent OS,
installer, authenticated account system or physical-hardware release.

## Requests and exact byte policy

Public declarations are in `include/umicom/kernel/fat16_lifecycle.h` and
`include/umicom/kernel/fat16_lifecycle_commit.h`. The request carries an
operation, absolute path, input pointer, byte count, truncate size and explicit
calendar value. Unused fields have one canonical value rather than silently
changing the meaning of another operation.

| Operation | Required input | File and allocation result |
| --- | --- | --- |
| CREATE | 0..4096 bytes; input NULL exactly for zero bytes; size zero; valid calendar | New regular short-name entry; allocate exactly the required new clusters. Existing names return EXISTS. |
| APPEND | 1..4096 bytes; non-NULL input; size zero; valid calendar | Append at implicit EOF, using retained capacity first and then allocating. A zero-length file may acquire its first cluster. |
| TRUNCATE | Input NULL; bytes zero; size no greater than the old file size; valid calendar | Retain the required prefix and release the remaining chain. Equal size and zero are admitted. |
| DELETE | Input NULL; bytes and size zero; every calendar field zero | Mark the selected entry deleted and free its complete chain. |

Paths use the existing portable absolute 8.3 grammar, canonicalised to
uppercase. CREATE refuses every existing sibling, including a directory or
casefold-equal alias. It honours the final parent directory's READ_ONLY bit;
the fixed root has no directory-entry attribute record. The other operations
refuse root, directories and READ_ONLY regular files. Existing siblings,
ancestor ownership and the complete bounded namespace remain part of the
allocation proof, even when only one file will change.

Every newly allocated cluster is completely zero-initialised before its
payload is overlaid. This includes sectors beyond the new EOF. Appending into
existing allocation changes only the caller's byte span, preserving the
surrounding captured sector bytes. Truncation preserves retained-tail bytes
past the new EOF and the contents of freed clusters. Deletion also retains
freed data bytes. Reallocation zeroes those clusters again. These operations
do not promise secure erasure.

The directory policy is deliberately precise:

| Operation | Permitted directory changes |
| --- | --- |
| CREATE | Clear the selected 32-byte record, write the uppercase padded alias, set ARCHIVE, supplied creation/write calendar and access date, first cluster and size. Creation tenths and other unused fields start at zero. |
| APPEND | Set ARCHIVE and write calendar, update size and the first cluster if an empty file gains allocation. Retain all other metadata. |
| TRUNCATE | Set ARCHIVE and write calendar, update size and first cluster when the file becomes empty. Retain all other metadata. Equal size still publishes the supplied write calendar. |
| DELETE | Change only the selected entry's first byte to E5. Retain all other bytes in the tombstone and containing sector. |

The FAT format defines the entry fields, deletion/end markers, cluster links
and clean-shutdown flags. The explicit timestamp requirement, deterministic
allocation, complete new-cluster zeroing, equal-size truncate behaviour and
strict supported profile are Umicom policies. The supplied calendar is
validated as a 1980..2107 Gregorian date; seconds are floored to two-second
precision. No host time, RTC value, elapsed Kernel ticks or timezone is inferred.

CREATE uses the earliest available deleted record or end marker in the
existing parent allocation. It preserves the successor end marker when
consuming the current one could reveal previously hidden records. A successor
in another physical sector, including a fragmented parent, receives a
separate planned sector when its first byte must be cleared. An already-zero
successor needs no extra write. The final allocated directory slot needs no
successor. Reusing a deleted slot leaves the existing end marker in place.

## Bounded preparation and ownership

`UmicomKernelFat16PlanLifecycle` is read-only. It resolves the target or new
parent, proves exact existing chains, traverses the bounded namespace, checks
both complete FAT copies, and classifies every usable cluster as owned or
free. Cross-links, orphan allocation, surplus chains, unsupported records,
bad-cluster entries, dirty flags and mismatched copies remain refusals. No
caller-supplied sector address becomes write authority.

| Bound | Limit |
| --- | --- |
| Caller payload | 4096 bytes per CREATE or APPEND |
| New clusters | 8 per operation, selected by increasing free identifier |
| Complete data sectors | 72, including zero-only initialisation sectors |
| Distinct planned FAT sectors | 32, always including header sector zero |
| Planned directory sectors | 2 |
| Exact existing/resulting file chain | 256 clusters |
| Live directory entries | Existing 128-entry profile; CREATE checks the resulting count |
| Namespace objects | Existing 256-object profile including root; CREATE checks the resulting count |
| Planner reads | One total 4096-read budget, retaining existing geometry and namespace bounds |

NO_SPACE, appended to the disk status enum, means the qualified volume has
insufficient free clusters or no available existing parent slot. LIMIT means a
profile bound was reached. A capacity request must not turn either condition
into a partial plan. Every planner error preserves the entire caller output,
including padding.

The plan stores original and final clean FAT sectors, complete data sectors,
and original/final directory sectors. FAT header sector zero is always
present, even if its allocation entries are unchanged. Each FAT sector's
`changed` flag describes allocation-byte changes, not guard transitions.
The append bridge sector is identified for final submission within each FAT
copy; a retained truncation tail is identified for first submission. The
transport separately derives original dirty guards and the final dirty header.
Finish must publish the final allocation-bearing clean header, never restore
old allocation words from the original snapshot.

Supply stable, initially zero-filled workspaces. The volume, request, both
workspaces, output plan and complete path/payload extents must be pairwise
independent, naturally aligned where typed, and non-overflowing. Each path
byte is checked against protected storage before reading it. The request and
borrowed bytes are snapshotted before the first reader callback. Callbacks
must not mutate these objects or the medium, retain pointers or re-enter an
active owner. This is a trusted Kernel pointer contract, not a user-memory
probing interface. Admitted exits scrub scratch, retain self bindings and
retire the FAT cache; pre-admission refusals preserve scratch.

## Ordered publication and repeated operations

Keep the writable/FLUSH lease exclusively owned through calls and idle
intervals. The outer owner is single-use: never copy, reinitialise or reuse it
after Close, and never call the private embedded owners' APIs. Multiple
operations happen inside that one admitted lifetime.

1. Open acquires the qualified lease without mutation. Stage is allowed in
   READY or after the previous accepted Finish in COMMITTED.
2. Stage snapshots intent, opens a fresh clean inspector, proves the whole
   plan, re-verifies original FAT and directory sectors, closes the inspector
   and reserves the remaining verification reads before any mutation.
3. Write the original dirty FAT2 header and FLUSH, then the original dirty
   FAT1 header and FLUSH. Verify both guards.
4. CREATE and APPEND write every data sector, FLUSH and verify the complete
   sectors before publishing links. Publish changed FAT2 sectors, FLUSH and
   verify; then do the same for FAT1. Submit an old append bridge after the
   new-link sectors within each copy. Finally publish the directory.
5. TRUNCATE and DELETE publish the reduced or absent directory reference
   before releasing links. Publish FAT2 and FAT1 with their separate barriers
   and verification. Within each copy, submit a retained truncation tail
   before the other changed sectors.
6. Each directory sector has its own WRITE, FLUSH and readback. A cross-sector
   successor end marker is durable and verified before its new live entry.
7. Successful Stage leaves STAGED with both FATs dirty. Finish re-verifies all
   planned data, both final dirty FAT copies and all directory sectors. It
   writes the final clean FAT2 header and FLUSH, then FAT1 and FLUSH, verifies
   both final headers and checks the deadline.
8. Only accepted Finish increments `committedOperations` and returns
   COMMITTED. A subsequent Stage starts new per-operation evidence and fresh
   read/deadline budgets. Close releases resources without implicit Finish,
   WRITE, FLUSH or repair.

Submission order inside a group is not a promise about torn sectors or the
order in which a device persists unflushed writes. The protocol deliberately
keeps ordinary readers from accepting an unfinished dirty volume. It cannot
recover an interrupted update or certify arbitrary hardware caches.

Let D be planned data sectors, F changed FAT sectors in one copy, and R
planned directory sectors. A successful Stage observes D data WRITEs,
`2 + 2F + R` metadata WRITEs, and
`2 + (D ? 1 : 0) + (F ? 2 : 0) + R` FLUSH acknowledgements. Finish adds two
metadata WRITEs and two FLUSHes. No-data operations retain NOT_SUBMITTED with
false data durability/verification flags. Unchanged FAT links need no extra
allocation WRITE or FLUSH; their verification flags may still be true.

Data-sector counters include zero-only initialisation. Payload byte counters
count only caller bytes, so confirmed payload bytes can reach their total
before all initialisation sectors complete. `dataOutcome` becomes COMPLETED
only after every planned data sector completes; an unconfirmed sector remains
SUBMITTED_UNCONFIRMED. FAT counters exclude the initial/final guard writes;
the common metadata counters include them. Directory counters separately
record submissions, completions and individual FLUSH acknowledgements.

Pre-admission errors preserve output and history. A read-only preflight
refusal publishes its evidence, submits no mutation and leaves READY, also
after a preceding accepted operation. Once a WRITE is submitted, failure
leaves FAILED and only resource release is allowed. A second Stage while
STAGED is refused. Historical durable/verified flags report observations;
they never grant retry authority. Late failure can follow clean publication,
without acceptance or an incremented count. No rollback or re-dirtying is
attempted. Failed Open/Close can retain resources for a further Close;
successful Close scrubs private plans and input but retains result history and
the accepted count.

## Trusted console

```text
fatfsopen SLOT PART
fatfstime YYYY-MM-DDTHH:MM:SS
fatcreate PATH "TEXT"
fatappend PATH "TEXT"
fattruncate PATH SIZE
fatdelete PATH
fatfscommit
fatfsinfo
fatfsclose
```

CREATE, APPEND, TRUNCATE and DELETE each stage one operation. Run
`fatfscommit` after each successful Stage before choosing another. The
selected calendar persists across accepted operations and may be changed
while READY or COMMITTED. DELETE requires no calendar. `fatcreate PATH ""`
creates an empty file. The console's existing bounded line length is smaller
than the Kernel API's 4096-byte payload limit.

The lifecycle console has its own owner and calendar. It shares the
file/append/rename busy guard, held through binding and output callbacks.
All three families require one shell identity while bound. Combined shell
shutdown releases each owner without completing or repairing unfinished
media. Diagnostics use `fat.lifecycle.*` and `fat.lifecycle-state`; common
operation evidence remains under `fat.commit.*`.

The ordinary system image still boots without an attached disk. To exercise
the new commands manually, copy the checked source fixture after building:

```powershell
Copy-Item .\tests\fat16_file_commit\fixture.raw .\build\manual-fat16-lifecycle.raw
```

Add these arguments to the normal `umicom-system.elf` QEMU boot command:

```powershell
    -global virtio-mmio.force-legacy=false `
    -drive "file=.\build\manual-fat16-lifecycle.raw,if=none,format=raw,id=umicom_files,cache=writeback" `
    -device "virtio-blk-device,drive=umicom_files,write-cache=on"
```

<!-- The console exposes disk discovery as "disks". The earlier command
name below is retained for review; use the corrected instruction after it.
Use `blocks` to discover the slot; the qualified QEMU layout reports slot 7
and the fixture's first primary partition is index 0. Enter inside the Kernel:
-->
Use `disks` to discover the slot; the qualified QEMU layout reports slot 7
and the fixture's first primary partition is index 0. Enter inside the Kernel:

```text
fatfsopen 7 0
fatfstime 2026-10-07T14:25:37
fatcreate /NOTES.TXT ""
fatfscommit
fatappend /NOTES.TXT "Persistent file lifecycle"
fatfscommit
fattruncate /NOTES.TXT 10
fatfscommit
fatfsinfo
fatfsclose
poweroff
```

Boot the same copy again with `readonly=on` in the drive string. Use
`fatstat 7 0 /NOTES.TXT` and the existing read commands to inspect the
persisted file. A separate writable boot can delete it with `fatdelete` and
`fatfscommit`. Keep the checked source fixture unchanged. Closing after Stage
without Finish intentionally leaves a dirty image that an ordinary fresh
reader refuses; that copy is interruption evidence, not a recovered volume.

## Build and qualification

The existing Windows incremental commands include the new guest journeys:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

`cmake/Fat16Lifecycle.cmake` registers six fresh writer/readback transitions:
create 700 bytes, append 900, truncate to 513, delete, create 700 reusing freed
clusters, and append 600 to the original empty file. Every transition compares the
complete 8 MiB image against an independent oracle. Four checkpoint forks
omit Finish after CREATE, APPEND, TRUNCATE and DELETE, then boot separate
read-only refusal guests. Dependencies capture each checkpoint before the
main image advances. Exact role/phase markers, process exit, finite timeout
and complete before/after hashes are runner gates. The source fixture cannot
be selected for mutation or cleanup.

Native qualification uses the real parser, planner, transport, console and
allocator with independent expected media and cached/eager device models.
It exercises allocation/FAT-sector boundaries, complete zeroing, metadata and
neighbour preservation, directory continuation, profile refusals, buffer
contracts, request faults, protocol cuts and repeated accepted operations.
The standalone image verifier constructs expected bytes independently of the
production planner. The delivery review records actual final inventories,
compiler configurations, failures corrected during development, stable
consumed inputs, exact guest results and the delivered ZIP overlay build.

On Linux or WSL, the separate native suite can be run with:

```bash
cmake -S tests/fat16_lifecycle -B build/native-fat16-lifecycle -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
    -DUMICOM_FAT16_LIFECYCLE_SANITIZERS=ON
cmake --build build/native-fat16-lifecycle --parallel 2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
LSAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/native-fat16-lifecycle --parallel 2 \
    --output-on-failure --no-tests=error
```

Emulated restart, software fault injection and native sanitiser results do
not claim physical power-loss testing, Windows execution or hardware
qualification. Functional media checks and disposable-file cleanup are
reported separately; a passing cleanup stage is not substituted for a later
observed absence check.

## References

- Microsoft, [FAT specification, version 1.03, 6 December 2000](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf),
  pages 18 and 22–25: allocation entries, FAT16 flags and short-directory fields.
- `KERNEL_AND_OS_RELEASE_ROADMAP.md`: persistent-system acceptance direction.
- `SOURCE_PRESERVATION_AND_NAMING.md`: preservation, authorship and naming.
- `FAT16_ORDERED_COMMITS.md`, `FAT16_FILE_APPEND.md` and
  `FAT16_FILE_RENAME.md`: preceding operation contracts.
