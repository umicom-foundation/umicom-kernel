# Bounded FAT16 file rename

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and boundary

`UmicomKernelFat16PlanRename` prepares a same-directory short-name rename of
one existing regular file. The dedicated `UmicomKernelFat16RenameCommitter`
stages the containing directory sector while the volume is dirty and requires
an explicit Finish before accepting a clean publication. A fresh read-only
boot then finds the new name, refuses the old name and reads the original
contents and metadata.

This advances the persistent-filesystem save/reboot/read work recorded in
`KERNEL_AND_OS_RELEASE_ROADMAP.md`. The native Kernel remains independent of
the Framework, GUI and Data Server. The separate GNU Linux-libre production
direction remains as recorded in the existing architecture decisions.

This operation accepts empty and nonempty regular files. It does not create
or replace a file, move an entry between directories, rename a directory,
allocate or free a cluster, update a calendar, or expose general writable VFS
access. It provides interruption detection, not atomicity across power loss,
a journal, rollback or repair. Existing overwrite and append APIs keep their
contracts.

## Names, collisions and entry preservation

The source is an absolute short-name path. The replacement is one portable
8.3 component, at most twelve characters before its terminating NUL. It uses
the existing inspector's ASCII alphabet and is canonicalised to uppercase.
Slashes, traversal, spaces, a trailing dot, a missing base name and long names
are refused. The replacement never selects a different parent directory.

Any existing casefold-equal sibling returns `UMICOM_DISK_EXISTS`, whose
diagnostic name is `exists`. This includes the source's own current alias: a
same-name or case-only request is refused rather than treated as a successful
no-op. A deleted directory record is not a sibling. The same alias in a
different parent does not collide. Root and directory sources return
`IS_DIRECTORY`; a READ_ONLY regular source is refused.

The complete planned directory sector permits only these changes:

| Entry byte offsets | Operation |
| --- | --- |
| 0–10 | Store the replacement uppercase, space-padded short alias. |
| 12 | Clear the supported lowercase-display bits `0x18`. |

Attributes, including ARCHIVE, remain unchanged. Size, both first-cluster
fields, creation fields, access date, write calendar and every unrelated byte
remain unchanged. No file-data sector or allocation link is planned or changed.

This is an explicit name-only Kernel primitive. Preserving ARCHIVE is a
deliberate boundary of this API; it does not implement the general FAT-driver
policy of setting ARCHIVE on rename. The supported display-bit handling is
also an Umicom policy. The cited FAT specification describes the short-name
layout, entry offsets and clean-shutdown flag, but does not establish these
two policies as general rename semantics.

The ordinary persistence fixture demonstrates the exact byte boundary:
`/FRAG.BIN` becomes `/SAVED.BIN`, retaining size 1300, first cluster 4,
attributes zero and its absent write calendar. Five alias bytes differ after
completed publication. The verified STAGED image has those five differences
plus the two dirty-header flag bytes. The entire remaining 8 MiB image stays
identical.

## Planner and trusted-pointer contract

Public declarations are in `include/umicom/kernel/fat16_rename.h` and
`include/umicom/kernel/fat16_rename_commit.h`. Existing public structure
layouts are unchanged; the new disk status is appended to the status enum.

| Plan field | Meaning after successful planning |
| --- | --- |
| `originalEntry` | The original checked entry, including its old alias, size, attributes and first cluster. |
| `updatedName` | The canonical uppercase replacement alias with its terminating NUL. |
| `directorySector`, `entryOffset` | The physically re-identified containing sector and entry location. |
| `original` | The complete original containing directory sector, including all raw calendar fields. |
| `data` | That complete sector with only the permitted name and display-bit changes. |

Planning resolves the source and its root or fragmented parent, checks sibling
collisions, validates the exact file chain, and applies the existing strict
bounded namespace and allocation proof. Empty files must have zero size and
zero first cluster. Surplus chains, cross-links, orphaned allocation, dirty or
inconsistent FAT copies and unsupported records retain the writer's refusal
rules. A second physical pass re-identifies the selected short entry; callers
cannot supply the physical LBA.

The inspector, both workspaces, result and complete terminated strings must
have stable, pairwise disjoint, non-overflowing extents. Typed storage requires
natural alignment. String bytes are checked against protected object storage
before they are read. Both strings are copied before the first reader callback.
Callbacks must not mutate caller or owner storage or the exclusively owned
medium, retain pointers, or re-enter active owners. These are trusted Kernel
pointers, not a user-pointer probing ABI.

Every planner error preserves the complete output, including padding.
Refusals before admission also preserve the workspaces. Admitted exits scrub
scratch while retaining self bindings, retire the FAT cache and release busy
guards. Invalid replacement syntax is checked before planner admission;
source-path syntax is checked after admission and produces a scrubbed exit.
The plan remains a snapshot, not authority to mutate an unowned medium.

Planning retains the total 4096-read budget. The enclosing transport applies
its own operation read budget and monotonic deadline, including preflight and
verification. The read-only inspector is closed before transport mutation.

## Owner lifetime and ordered persistence

Open, Stage once, explicitly Finish, then Close. Supply stable, initially
zero-filled owner storage. Retain exclusive medium ownership throughout each
call and every idle interval. Do not copy or reinitialise an admitted owner,
or call APIs on its private embedded committer, updater and workspaces.

| Phase | Operation | Successful evidence |
| --- | --- | --- |
| Open | Acquire the qualified writable/FLUSH lease without changing media. | READY |
| Preflight | Validate storage, prove the rename and clean headers, close the inspector, reserve verification reads. | No WRITE or FLUSH |
| Mark dirty | Write FAT2 header and FLUSH; write FAT1 header and FLUSH; verify both headers. | Dirty headers durable and verified |
| Stage directory | WRITE, FLUSH and verify the complete renamed directory sector; check the deadline. | STAGED; 3 metadata writes and 3 FLUSHes |
| Finish | Re-verify dirty headers and the complete directory sector. Restore clean FAT2, then clean FAT1, each with FLUSH. Verify headers and the final deadline. | COMMITTED; 5 total metadata writes and 5 FLUSHes |
| Close | Release owned resources without implicit Finish, WRITE, FLUSH or flag repair. | CLOSED when release succeeds |

There is no data-write or data-flush phase. `commit.offset`, requested,
submitted and confirmed bytes, and all data-write counters remain zero.
`dataOutcome` remains `NOT_SUBMITTED`; `dataDurable` and `dataVerified` remain
false. Metadata counters include the two FAT headers and directory writes.
The rename-specific directory fields separately record planning, submission,
completion, durability and complete-sector verification.

`directoryPlanned` qualifies the original entry, replacement alias and sector
address. Pre-admission Stage refusals preserve output bytes and historical
results. Destination grammar and collision refusals through Stage can follow
read-only inspector admission; they publish preflight evidence, leave READY
and submit no WRITE or FLUSH. A refusal after metadata submission leaves
FAILED, closeable only. A second Stage on STAGED is refused unchanged.

Finish requires the qualified directory and dirty-header evidence, an unused
data plan and zero data-operation evidence. A failure can occur after a
successful FLUSH or even after the medium has become clean. Historical flags
describe observed progress; they never grant permission to retry or repair.
The committer does not roll back a renamed entry or re-dirty a possible clean
publication after a late error.

Failed Open can retain resources; retry only Close until release succeeds.
Failed cleanup retains CLOSING state and its backing resources. Successful
Close scrubs private plans and preserves historical results. Closing a
zero-filled, never-admitted owner is harmless. Closing a STAGED owner leaves
the image dirty, so a fresh ordinary reader refuses it without repair.

## Trusted console

```text
fatrenameopen SLOT PART
fatrenamestage PATH NEWNAME
fatrenameinfo
fatrenamecommit
fatrenameclose
```

Rename has its own metadata owner and requires no calendar input. It shares
the file/append console's busy guard, held through output callbacks, and
requires the same shell identity while either family remains bound. The
combined shell Close refuses callback re-entry even before the first owner
has been bound, then releases both owner families for the bound shell.

Diagnostics use `fat.rename.*` and `fat.rename-state`; common counters retain
their `fat.commit.*` names. Stage reports the original and replacement names
and leaves the image dirty. Acceptance requires `fatrenamecommit` to succeed.
`fatrenameclose` only releases resources.

The ordinary normal boot still works without a disk. For interactive rename,
first copy the checked-in fixture to a disposable path:

```powershell
Copy-Item .\tests\fat16_file_commit\fixture.raw .\build\manual-fat16-rename.raw
```

Attach that copy to the normal QEMU command with these additional arguments:

```powershell
    -global virtio-mmio.force-legacy=false `
    -drive "file=.\build\manual-fat16-rename.raw,if=none,format=raw,id=umicom_rename" `
    -device "virtio-blk-device,drive=umicom_rename"
```

<!-- The console exposes disk discovery as "disks". The earlier command
name below is retained for review; use the corrected instruction after it.
Use `blocks` to discover the slot. The fixture's first primary partition is
index 0; the qualified QEMU layout reports slot 7:
-->
Use `disks` to discover the slot. The fixture's first primary partition is
index 0; the qualified QEMU layout reports slot 7:

```text
fatrenameopen 7 0
fatrenamestage /FRAG.BIN SAVED.BIN
fatrenameinfo
fatrenamecommit
fatrenameclose
poweroff
```

Start a fresh normal boot with the same copy attached read-only, adding
`,readonly=on` to that drive string. Use `fatstat 7 0 /SAVED.BIN` to inspect
the retained metadata and `fatstat 7 0 /FRAG.BIN` to check the old name is
absent. The existing read commands inspect the original contents. Keep the
checked-in fixture unchanged.

## Reproducible qualification

The ordinary Debug workflow builds and runs the rename guests alongside the
existing Kernel tests:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

`cmake/Fat16Rename.cmake` adds writer, fresh readback, controlled interruption
and rejected-reader guest roles. Fixture dependencies create independent
completed and interrupted copies. The runner checks exact ordered role
markers, process status, finite timeouts and whole-image SHA-256 values before
and after every guest. Writer and cleanup paths reject the checked-in source
fixture. C and host-side oracles independently construct the expected
complete-image bytes; no native helper is required by the cross build.

The writer checks name and source refusals before mutation, then verifies
the metadata-only counter progression and exact staged or committed image.
The fresh reader verifies old-name absence, new-name contents and metadata,
unchanged other files, and continued refusal of VFS write rights. The
interruption role omits Finish after verified Stage and Close. A separate
read-only guest must refuse that dirty volume, preserve output guards and
leave its complete image unchanged. This is a deterministic restart at a
protocol boundary, not a physical power-cut experiment.

Native tests use the real planner, transport, console and allocator with
independent cached and eager media models. They check every observed request
fault, verified protocol cuts, directory/header tamper, deadlines, lifecycle,
pointer guards, empty and fragmented-parent files, name collisions and whole
image preservation. The request oracle rejects every WRITE outside the two
FAT header sectors and selected directory sector. Console tests cover both
cross-family binding directions and callbacks before initial binding.

On Linux or WSL with Clang, CMake and Ninja:

```bash
cmake -S tests/fat16_rename -B build/native-fat16-rename -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
    -DUMICOM_FAT16_RENAME_SANITIZERS=ON
cmake --build build/native-fat16-rename --parallel 2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
LSAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/native-fat16-rename --parallel 2 \
    --output-on-failure --no-tests=error
```

The delivery review records actual case counts, toolchain versions, source
hashes, full Debug/Release guest results, manual console journeys and the
incremental build from the prior committed tree using the delivered ZIP.
Native sanitiser runs qualify memory and undefined-behaviour checks with leak
scanning disabled in the hosted environment. Emulator results do not claim
Windows execution, physical hardware qualification or real power-loss tests.

## References

- Microsoft, [FAT specification, version 1.03, 6 December 2000](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf),
  pages 18 and 23–24: FAT16 header flags, short-entry layout and name encoding.
- `SOURCE_PRESERVATION_AND_NAMING.md`: existing lines, authorship and naming rules.
- `KERNEL_AND_OS_RELEASE_ROADMAP.md`: persistent-filesystem acceptance direction.
- `FAT16_FILE_APPEND.md` and `FAT16_ORDERED_COMMITS.md`: preceding bounded data operations.
