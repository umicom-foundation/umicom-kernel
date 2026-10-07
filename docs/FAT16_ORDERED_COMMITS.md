# Ordered FAT16 commits and interruption detection

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## What this milestone provides

The Kernel can make a bounded change to an existing FAT16 file while recording
an unfinished update on the disk itself. The new owner establishes dirty flags
before changing file data, flushes and verifies the new bytes, then requires a
separate explicit call to finish clean finalisation.

This follows the original checked file-data updater. That API, its five console
commands and its metadata-preservation contract remain intact. The new API adds
an ordered protocol around the same whole-volume planner and block transport.
It is useful for qualifying interruption detection before introducing ordinary
writable filesystem descriptors.

The admitted update still contains 1–4096 bytes wholly within an existing file's
size and allocation. The target must have READ_ONLY clear and ARCHIVE already
set. Directory records, timestamps, names, file size and allocation are retained.
The temporary metadata changes are precisely the clean-shutdown bit in each
FAT's second reserved entry. Successful finalisation restores both original FAT
header sectors.

This is native Kernel C23 work. It introduces no Framework, GUI, Data Server or
OS-distribution dependency. The separate GNU Linux-libre production direction
and native Kernel research direction retain their roles in the release roadmap.

## The problem addressed

Suppose a console changes part of a saved configuration file and the operation
stops after writing the first sector. A counter in RAM can describe that partial
write while the Kernel is running, but the counter disappears on restart. A new
reader needs an on-disk indication that normal access must not proceed.

The FAT dirty flag provides that indication. It does not contain the old file
contents, the intended new contents, a transaction identifier or instructions
for repair. This batch therefore detects an incomplete update and refuses it;
it does not reconstruct a file or choose whether old or new data should win.

The [Microsoft-authored FAT specification, section 4.2](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf)
defines clean bit `0x8000` and no-I/O-error bit `0x4000` in FAT16 entry one.
The admitted clean value is `0xffff`; this protocol's dirty value is `0x7fff`.
It preserves the no-error bit instead of treating it as an in-progress marker.
Recording an actual hard I/O error remains a separate, unimplemented operation.

The ordinary inspector and read-only provider keep their existing rules. They
refuse initial dirty flags and inconsistent FAT copies. No reader fabricates a
clean header to work around those checks.

## The exact operation order

FAT1 below means the first/primary FAT; FAT2 means the second/mirror FAT.
Every WRITE submits a complete 512-byte sector. The original first FAT sector
contains allocation entries as well as flags, so every other byte must be kept.

| Step | Operation | Evidence required before advancing |
| --- | --- | --- |
| Open | Acquire an exclusive writable lease with FLUSH support; read initial geometry | Clean FAT16 admission and an owned handle |
| Plan | Reopen the inspector; prove whole-volume ownership; stage patched data sectors and exact header bytes | Every bound, path, allocation and read check succeeds |
| Retire inspection | Close the inspector and reserve the remaining readback budget | No active immutable-medium interpretation remains |
| Dirty FAT2 | Write the mirror's dirty header; FLUSH | Validated write and flush completions |
| Dirty FAT1 | Write the primary's dirty header; FLUSH | Validated write and flush completions |
| Verify dirty guards | Read and compare both complete dirty header sectors | Both expected sector images match |
| Apply data | Write staged sectors in logical file order; FLUSH | Complete data-write and data-flush observations |
| Verify data | Read and compare every complete affected data sector | Requested bytes, neighbours and slack match the plan |
| Return from Stage | Publish the cumulative result | State is STAGED; both FAT headers remain dirty |
| Begin Finish | Recheck both dirty headers and all staged data sectors | The expected staged state is still observed |
| Clean FAT2 | Write the original mirror header; FLUSH | Validated write and flush completions |
| Clean FAT1 | Write the original primary header; FLUSH | Validated write and flush completions |
| Verify clean headers | Read and compare both original header sectors | Exact header match and final operation deadline accepted |
| Return from Finish | Publish accepted completion | State is COMMITTED |
| Close | Reset and release the transport and its DMA frames | Resource release succeeds, or ownership is retained for retry |

The primary FAT remains the dirty guard during the first clean-header step.
Both copies are required to agree when the ordinary inspector later opens the
volume. The selected mirror order is an implementation decision, not a claim
that the FAT specification prescribes this particular algorithm.

The [VirtIO 1.2 device-operation contract](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
distinguishes a completed WRITE from stable storage: an appropriate subsequent
completed FLUSH establishes the negotiated persistence boundary. Readback adds
an observation of the returned bytes. Neither observation can make a dishonest
backend, external writer or unrelated hardware failure safe.

## Public lifetime and state rules

The declarations are in `include/umicom/kernel/fat16_commit.h`.

| API | Meaning |
| --- | --- |
| `UmicomKernelFat16CommitOpen` | Admit a separate writable/FLUSH owner; this call reads but does not mutate the disk |
| `UmicomKernelFat16CommitStage` | Apply and verify one bounded update with persistent dirty flags; leave it STAGED |
| `UmicomKernelFat16CommitFinish` | Recheck the staged state and explicitly finish clean finalisation |
| `UmicomKernelFat16CommitClose` | Release resources without flush, finalisation or flag repair |

The APIs reuse the existing `UmicomKernelFat16UpdateStatus` vocabulary. The
result also carries exact disk and block statuses and the operation phase.
A readback mismatch is FILESYSTEM_ERROR with DISK_CORRUPT. A read failure keeps
the exact transport status. A read-budget admission refusal reports
INSPECTION_LIMIT with DISK_LIMIT.

| State | Permitted next work |
| --- | --- |
| UNUSED | Open, or harmless Close |
| READY | Stage, or Close; a preflight refusal can be corrected and retried if the transport remains usable |
| STAGED | Finish, or resource-only Close |
| FAILED | Close only; a failed protocol is never resumed automatically |
| COMMITTED | Close only |
| CLOSING | Retry Close until the original handle and frames can be released |
| CLOSED | Harmless repeated Close; an admitted owner cannot be reopened |

An error after any metadata submission enters FAILED, even when no file-data
sector was submitted. A first header write can itself have changed uncertain
bytes. A second Stage cannot replace its evidence or accidentally normalise an
interrupted operation. A successful Stage likewise admits no second update.

Stage and Finish each have their own 100,000,000-tick enclosing acceptance bound,
in addition to the existing transport timeout and finite polling bound. At the
qualified QEMU clock frequency, that enclosing interval is ten seconds.
The interval between those calls has no automatic timer or expiry: the caller
continues to own the backing medium and must decide to Finish or Close.

The Stage reader has a total 4096-call budget, including admission, planning,
header snapshots and verification. Before the first metadata write it reserves
two dirty-header reads plus one read per staged data sector. A known shortage
therefore refuses the operation without unnecessarily dirtying the volume.

## How to read the result

`UmicomKernelFat16CommitResult` is cumulative across Stage and Finish. Its
durable/verified fields are historical observations. For example, a successful
Finish still has `dirtyDurable` set because the earlier dirty barrier completed;
that field does not say that the disk is currently dirty.

| Evidence | Interpretation |
| --- | --- |
| `phase` | Last entered protocol phase, including the phase where a failure stopped progress |
| `status`, `diskStatus`, `blockStatus` | High-level result and exact lower-layer status |
| `lastBlockOutcome` | Latest attempted WRITE/FLUSH observation, including during subsequent verification reads |
| `confirmedBytes`, `submittedBytes` | Validated caller-byte prefix and the caller bytes covered by all submitted data sectors |
| Data-sector counters | Fully completed and submitted file-sector requests |
| Metadata-sector counters | Fully completed and submitted FAT-header requests |
| `completedFlushes` | Validated successful FLUSH completions |
| `uncertainSectorValid`, `uncertainSector` | Entire physical sector at risk after a submitted but unconfirmed WRITE |
| `dirtyDurable`, `dirtyVerified` | Both dirty barriers completed and both full dirty header sectors were subsequently observed |
| `dataDurable`, `dataVerified` | Data FLUSH completed and all full affected data sectors were subsequently observed |
| `cleanFinalisationStarted` | At least one clean-header WRITE was submitted |
| `cleanDurable`, `cleanVerified` | Final clean FLUSH completed and both full clean headers were subsequently observed |
| `commitAccepted` | Finish reached complete clean verification and passed its final acceptance check |
| `needsFlush`, `writeUncertain` | Transport-owner observations at the API return |

A late enclosing timeout can reject an operation after the block driver already
reported a valid successful completion. Counters and barrier observations remain
true; the status remains an error. A non-OK result never implies rollback.

An uncertain header write puts the complete sector at risk, including allocation
entries beside the reserved flags. An uncertain file-sector write puts its
neighbours and slack at risk as well. Confirmed caller bytes do not identify an
automatic retry point. The result is preserved across Close and failed cleanup.

## Interruption and final acknowledgement

Before the first persistent dirty guard, an interrupted operation can leave
clean original media or a dirty/mismatched header. It has not submitted any file
data. After both dirty barriers and before clean finalisation, the established
guards remain available across restart, subject to the storage contract.

There is an essential finalisation case: the final clean write or flush can
reach storage while its acknowledgement is lost. The API must then report an
unconfirmed or otherwise rejected completion, while a fresh reader may observe
a clean volume containing the complete new data. A readback error after clean
publication can produce the same high-level uncertainty.

Consequently, this implementation does not promise that every failed call will
leave a dirty volume. Once clean publication starts, it never attempts to
re-dirty the volume or repeat the data automatically. Doing so could damage an
already completed update or overwrite subsequent work.

The dirty flag records an unfinished session, not an exactly-once operation ID.
A recovery design must supply separate evidence and explicit policy before it
can decide whether to recover, roll back, replay or accept an uncertain result.

## Memory, ownership and source organisation

The committer embeds an existing updater to reuse its checked lease, clock,
reader, planner and cleanup mechanisms. The appended implementation lives in
`kernel/fat16_update.c`; the original updater functions remain intact. The
embedded object is private storage, not another public handle callers may use.

The whole outer owner must be independent of its block domain and every
retained DMA frame. Input, terminated path and result must also be mutually
independent and outside all of that storage. Each candidate path byte is checked
for aliasing before it is read, including when the path is not terminated.
These are trusted Kernel pointers, not user-memory probing operations.

Both outer and embedded reentry guards are established before callbacks.
They provide serial ownership checks; they are not multi-hart locks. The current
platform still requires its qualified hart-zero, interrupts-disabled Bare
context and coherent-DMA assumptions. Caller callbacks and media remain stable.

The committer keeps original/dirty header images, readback scratch and the
existing private data plan. No large automatic sector arrays or new physical
allocations are introduced. Close scrubs the private data plan during cleanup
and clears header/readback scratch after transport release succeeds, while
leaving the historical result available.

The console implementation is appended to `kernel/fat16_update_console.c`,
reusing existing formatting helpers. The new shell hooks are conditional and
additive. Earlier descriptions remain in their original fallback build paths.
All modified files retain every original source line, comment and credit.

## Console workflow

Use the disposable-image command in [the validation guide](FAT16_COMMIT_VALIDATION.md).
Inspect `disks` and use the reported slot; slot 7 is only the example used by
the qualified QEMU attachment.

```text
disks
fatcommitopen 7 0
fatstage /README.TXT 0 "x"
fatstage /FRAG.BIN 511 "Umicom ordered update"
fatcommitinfo
fatcommit
fatcommitinfo
fatcommitclose
poweroff
```

The README attempt is refused as read-only and does not dirty the volume. The
FRAG operation changes 21 caller bytes across two fragmented data sectors.
After Stage, data has been flushed and verified but the disk remains dirty.
After Finish, the historical record reports an accepted commit and both original
clean header sectors have been restored and verified.

`fatcommitinfo` labels historical observations separately from current owner
state. A refused Stage or Finish reports `not-admitted` and retains the previous
result. Output and I/O callbacks cannot re-enter the active console owner.
The owner is bound to the shell which opened it, including retained admission
failures that still need cleanup.

For the deliberate interruption example, create a fresh disposable copy and
omit `fatcommit` after a successful Stage. Close or power off normally. The
subsequent fresh read-only process must refuse the dirty volume. Normal shutdown
is sufficient to demonstrate that Close does not silently finish the protocol;
it is not presented as a physical power-cut experiment.

## Work still required for a writable filesystem

The new protocol does not add create, resize, truncate, rename, remove, free-space
allocation, timestamps, automatic hard-error recording, journals, recovery,
checksums, authentication, snapshots or general writable VFS descriptors.
It does not relax the existing bounded short-name and whole-volume profile.

Ordinary FAT file modification also requires appropriate write timestamps and
archive handling, as described in sections 6.2–6.3 of the FAT specification.
This trusted data utility deliberately retains its narrower scope. A later
metadata stage should derive directory-entry locations from the existing
decoder, accept an explicit valid time source, and define its failure ordering
before changing ordinary file metadata or exposing writable VFS access.

The persistent-system release gate remains open: an installer, persistent
accounts/configuration, recoverable boot and the remaining system services are
separate work. Completed native and QEMU qualification for this precise storage
increment is recorded in the companion validation guide and delivery review.
