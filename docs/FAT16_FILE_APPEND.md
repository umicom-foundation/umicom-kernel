# Bounded FAT16 file append

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Purpose and boundary

`UmicomKernelFat16PlanFileAppend` and `UmicomKernelFat16FileCommitAppend`
extend a nonempty regular file at its current end, using only unused bytes in
its already allocated final cluster. The existing file committer owns the
medium, stages the data and enlarged directory entry, and explicitly finishes
the ordered commit. A fresh read-only boot can then observe the increased size,
appended contents, ARCHIVE attribute and supplied write calendar.

This advances the persistent-filesystem save/reboot/read work recorded in
`KERNEL_AND_OS_RELEASE_ROADMAP.md`. The native Kernel remains independent of
the Framework, GUI and Data Server. The separate GNU Linux-libre production
direction remains as recorded in the existing architecture decisions.

The operation accepts 1–4096 input bytes. It allocates no cluster, changes no
allocation link, and does not create a file or populate an empty file. It does
not provide sparse writes, general writable VFS access, a transaction journal,
rollback, repair or atomic publication across power loss. The existing
`UmicomKernelFat16PlanFileUpdate` and `UmicomKernelFat16FileCommitStage`
interfaces retain their fixed-size overwrite contracts.

## Append geometry

Let `S` be the original file size, `C` the cluster size in bytes, and `N` the
requested append count. The checked chain must contain exactly `ceil(S / C)`
clusters. Planning requires a nonzero original size and first cluster, and:

```text
1 <= N <= 4096
S + N <= UINT32_MAX
S + N <= ceil(S / C) * C
```

The original EOF is the only permitted append offset. A caller cannot choose
an offset or introduce a gap. A file whose size already ends at a cluster
boundary has no admissible slack. Surplus allocated clusters are a malformed
chain under the existing writer proof; they do not grant additional capacity.
Directories, read-only files and unsupported or inconsistent volumes retain
their established refusal statuses.

The ordinary persistence fixture illustrates the boundary. `/FRAG.BIN` has
1300 bytes in three 512-byte clusters, giving 1536 allocated bytes and 236
unused bytes. The dedicated guest appends 197 bytes and records a size of
1497. Its original 1300 bytes and remaining 39 slack bytes stay identical.
A 237-byte request is refused before any WRITE or FLUSH.

## Planner and value contract

The public declarations and full pointer/owner contract are in
`include/umicom/kernel/fat16_file_append.h`. The planner reuses the established
data plan, directory plan and workspace types; no existing public structure
layout changes.

| Result field | Meaning after successful append planning |
| --- | --- |
| `outDataPlan.entry` | The original checked directory entry, including the old size. |
| `outDataPlan.offset` | The original EOF. |
| `outDataPlan.bytes` | The append count. |
| `outDataPlan.sectors` | Complete affected sectors, preserving all bytes outside the append span. |
| `outFilePlan.original` | The complete original containing directory sector. |
| `outFilePlan.data` | That sector with the selected entry's permitted fields updated. |

The enlarged size is `offset + bytes`. The data plan deliberately retains the
original entry because chain validation and sector mapping refer to the
existing allocation. The complete planned directory sector carries the new
size.

Planning resolves the short-name path and its containing root or fragmented
parent directory, checks the exact target chain, and uses the established
bounded whole-volume namespace and allocation proof. A second physical pass
re-identifies the directory record. The caller never supplies its physical
LBA. Long-name records are refused under the existing writer profile.

Path, payload and calendar fields are copied before the first reader callback.
Owners, workspaces, outputs and input extents must be independent, stable and
non-overflowing; typed objects must be naturally aligned. Callbacks must not
edit caller or owner storage, retain pointers, mutate the exclusively owned
medium, or re-enter an active owner. These are trusted Kernel pointers, not a
user-pointer probing ABI.

Both planner outputs, including padding, remain unchanged on every refusal.
Admitted exits scrub the workspaces while retaining their self bindings, retire
the FAT cache and release busy guards. A refusal before workspace admission
does not initialise or alter an untouched workspace. Planning uses one total
4096-read budget; the enclosing transport also enforces its operation budget
and monotonic deadline. A plan is a snapshot and does not confer ownership of
the medium.

## Directory and byte preservation

Only these selected short-directory-entry fields may change:

| Entry byte offsets | Permitted change |
| --- | --- |
| 11 | Set ARCHIVE (`0x20`), preserving the other accepted attributes. |
| 22–25 | Store the explicit write time and date. |
| 28–31 | Store the enlarged 32-bit file size in little-endian order. |

The write calendar must describe a real Gregorian date in 1980–2107. Seconds
are floored to FAT's two-second precision. No RTC, current date or timezone is
inferred. For example, the guest's deliberately distinctive
`2044-02-29T23:58:57` becomes `2044-02-29T23:58:56`, with raw time `0xbf5c`
and raw date `0x805d`. These offsets, ranges and encodings follow the FAT
specification cited below; the operation's refusal policy is Kernel-specific.

The short name, creation fields, access date, first-cluster fields, other
directory records, unaffected sector bytes and allocation links stay intact.
At most nine entry bytes are eligible to change; fewer may differ when a
field already contains its new value. FAT header clean flags change only as
part of the established ordered persistence protocol.

## Owner lifetime and ordered persistence

Open through `UmicomKernelFat16FileCommitOpen`, append once, explicitly call
`UmicomKernelFat16FileCommitFinish`, then close. Retain exclusive medium
ownership during each call and every idle interval. Owners are single-use;
do not copy them, reinitialise an admitted owner, or access its private embedded
owners and workspaces.

| Phase | Operation and evidence | State |
| --- | --- | --- |
| Admission and preflight | Validate storage, copy input, prove the volume and planned sectors, reserve the required verification reads. No mutation. | READY |
| Mark dirty | Write FAT2 header, FLUSH, then FAT1 header, FLUSH; verify dirty headers. | Operation active |
| Append data | Write complete affected sectors, FLUSH and read back every planned sector. | Operation active |
| Publish enlarged directory entry while dirty | Write the containing directory sector, FLUSH and verify the complete sector; check the final deadline. | STAGED on success |
| Explicit Finish | Re-verify dirty headers, planned data and enlarged directory sector; restore clean FAT2 then clean FAT1, each with FLUSH; verify headers and final deadline. | COMMITTED on success |
| Close | Release owned resources and report cleanup evidence. No implicit Finish, WRITE or FLUSH. | CLOSED when release succeeds |

A read-only preflight refusal leaves the owner READY. A failure after a
metadata submission leaves it FAILED and closeable only. A later retry or
repair is not authorised by a historical durability flag. A failed or
interrupted operation can leave partially published contents and metadata;
the dirty protocol provides refusal evidence for a fresh ordinary reader,
not recovery or atomicity.

`UmicomKernelFat16FileCommitResult` is reused. For append,
`directoryPlanned=true` also qualifies `commit.offset` as the original EOF.
`commit.requestedBytes` is the append count, and their sum is the planned size.
Without `directoryPlanned`, an initial zero offset is not an observed EOF.
Pre-admission refusals preserve prior result bytes and history. Other counters,
durability fields and cleanup rules retain the existing committer meanings.

The original Stage implementation is retained verbatim under a labelled
inactive block to meet source-preservation requirements. Its active wrapper
and the new Append wrapper share the same ordered commit body, differing only
in their planner selection. Existing overwrite regression suites qualify that
shared path as well as the append-specific suite.

## Trusted console

```text
fatfileopen SLOT PART
fatfiletime YYYY-MM-DDTHH:MM:SS
fatfileappend PATH "TEXT"
fatfileinfo
fatfilecommit
fatfileclose
```

The command shares the existing file-commit owner, explicit calendar, shell
identity and busy guard. It uses the established bounded textual parser; the
4096-byte C API limit does not enlarge the console line limit. A calendar must
be supplied before append. Successful append reports the old and planned
sizes and leaves the volume dirty until `fatfilecommit` succeeds. Closing a
STAGED owner deliberately leaves that dirty image on disk.

The usual normal boot works without a disk. To exercise append interactively,
first copy `tests/fat16_file_commit/fixture.raw` to a disposable path such as
`build/manual-fat16-append.raw`. Attach that copy to the normal QEMU command
using the following additional arguments:

```powershell
    -global virtio-mmio.force-legacy=false `
    -drive "file=.\build\manual-fat16-append.raw,if=none,format=raw,id=umicom_append" `
    -device "virtio-blk-device,drive=umicom_append"
```

Use `blocks` to discover the slot. The fixture's first primary partition is
index 0. On the qualified QEMU layout the device appears in slot 7:

```text
fatfileopen 7 0
fatfiletime 2044-02-29T23:58:57
fatfileappend /FRAG.BIN "Umicom append survives a fresh boot."
fatfileinfo
fatfilecommit
fatfileclose
poweroff
```

The printable payload has 36 bytes, so this example produces a size of 1336.
Start a fresh normal boot with that same copy attached read-only, then use
`fatstat 7 0 /FRAG.BIN` and the existing read commands to inspect persisted
metadata and contents. The test calendar is an example, not a suggested
current timestamp. Keep the checked-in fixture unchanged.

## Reproducible qualification

The ordinary Debug workflow builds and runs the append guests alongside the
existing Kernel tests:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

`cmake/Fat16Append.cmake` provides writer, fresh readback, controlled
interruption and rejected-request guest roles. Its fixture dependencies create
separate writable copies for completed and interrupted operations. The runner
checks complete input and output image SHA-256 values, exact role markers in
order, process status and finite timeout. Writer and cleanup paths explicitly
refuse the checked-in source fixture. The complete and interrupted output
images are checked independently of the Kernel implementation.

The controlled interruption role stops after the append's verified FLUSH and
Close, omitting Finish, then boots a separate read-only guest. This is a
deterministic restart at a protocol boundary; it is not a physical power-cut
experiment. The reader must refuse the dirty volume without publishing a
partial metadata result or changing the image.

Native tests compile the real planner, updater, transport and allocator with
an independent disk model. On Linux or WSL with Clang, CMake and Ninja:

```bash
cmake -S tests/fat16_append -B build/native-fat16-append -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang \
    -DUMICOM_FAT16_APPEND_SANITIZERS=ON
cmake --build build/native-fat16-append --parallel 2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
LSAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/native-fat16-append --parallel 2 \
    --output-on-failure --no-tests=error
```

The suite includes alias and reentry guards, exact-capacity boundaries,
fragmented parent directories, calendar validation, allocation corruption,
read/write/FLUSH faults, data and directory tampering, deadline expiry and
clock rollback, cleanup ownership, console routing, and cached/eager
interruption models. The Stage read-budget case observes 4096 operation reads
(4100 including Open), with no WRITE or FLUSH; this is distinct from the pure
planner's 4096-read ceiling.

The maximum-input cases use a declared 67,584-sector device and 65,536-sector
partition, with 8 MiB of backed arrays and an assumed zero unallocated tail.
This permits a valid FAT16 volume with 16-sector clusters. The cases assert
that all actual requests remain within the backed range and report the
highest requested LBA. Their complete byte comparisons cover the backed
arrays; they do not materialise and compare the entire declared virtual
device or exercise reads from its unallocated tail. The QEMU persistence
cases instead compare every byte of the real
8 MiB raw image.

## Format reference

Microsoft, *FAT: General Overview of On-Disk Format*, version 1.03,
6 December 2000, sections on FAT directory structure and date/time fields:
<https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf>.
The implementation is original C23 Kernel code; no reference implementation
or new third-party dependency is introduced.
