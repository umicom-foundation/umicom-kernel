# Umicom Kernel — Writable VirtIO block leases and explicit flush

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Capability and architectural position

The existing modern VirtIO MMIO driver now supports bounded sector WRITE and
FLUSH operations through a separately admitted writable lease. This provides
the transport needed for later persistent filesystem work. It uses the existing
qualified hardware catalogue, platform adapter, physical allocator, queue owner
and reset protocol.

`UmicomKernelBlockOpen` retains its read-only admission contract. A trusted
Kernel caller must deliberately call `UmicomKernelBlockOpenWritable` to acquire
mutation authority. The checked FAT16 inspector, filesystem provider, mount
helper and disk console still use read-only admission and retain their current
rights. Ordinary console file commands continue to operate on RAMFS.

The Kernel remains independent of the Umicom Framework and the Umicom OS
distribution. This addition belongs to the native RISC-V Kernel research
target; it does not change the GNU Linux-libre basis of the production system.

## Public operations

All calls retain the trusted, serial Kernel execution contract: hart zero,
Bare addressing, interrupts disabled, a qualified coherent-DMA platform, and
stable domain and callback storage. The reentry guard is not a multi-hart lock.

| Operation | Admission or request contract |
|---|---|
| `UmicomKernelBlockOpen` | Require a modern backend advertising read-only storage. A writable backend still returns `UMICOM_BLOCK_WRITABLE_DEVICE`. |
| `UmicomKernelBlockOpenWritable` | Require a modern writable backend offering FLUSH. A read-only backend returns the appended `UMICOM_BLOCK_READ_ONLY` status. |
| `UmicomKernelBlockRead` | Read one through eight complete 512-byte sectors through either lease mode. Every non-OK result preserves the caller's data output. |
| `UmicomKernelBlockWrite` | Write one through eight complete 512-byte sectors through a writable lease and report an explicit mutation outcome. |
| `UmicomKernelBlockFlush` | Submit a FLUSH through a writable lease, including when its local dirty flag is clear, and report an explicit mutation outcome. |
| `UmicomKernelBlockClose` | Reset, obtain acknowledgement, scrub and release owned DMA frames. Retain incomplete cleanup for retry. |

An existing lease is never upgraded in place. Another open cannot take over a
claimed slot or reset a device whose nonzero status belongs to another driver.
The existing generation-tagged handle rules continue to reject stale owners.

Initialise the output handle to zero before either admission operation. An
admission failure normally rolls back its resources. If reset or release cannot
finish, the returned handle remains nonzero and must be retained for explicit
close and later cleanup retry. The primary admission error and retained cleanup
obligation are different observations.

## Feature negotiation and persistence boundary

Writable admission requires `VIRTIO_F_VERSION_1` and `VIRTIO_BLK_F_FLUSH` and
refuses `VIRTIO_BLK_F_RO`. It negotiates only the implemented modern and FLUSH
features. The driver does not negotiate configurable write-cache mode, packed
queues, indirect descriptors, event-index operation, multiple queues, discard
or write-zeroes.

The VirtIO block specification defines WRITE as request type 1 and FLUSH as
request type 4. Sector addresses are measured in 512-byte units. A FLUSH request
has sector zero and no data payload. With FLUSH negotiated and configurable
write-cache mode unnegotiated, the driver uses writeback semantics: a successful
WRITE needs a later successful FLUSH to establish the protocol's stable-write
condition. A persistent backend must honour that condition before reporting
FLUSH completion.

These are separate facts:

1. Request publication permits the device to act on the descriptors.
2. A fully checked WRITE completion acknowledges that particular write.
3. A later fully checked FLUSH completion provides the negotiated persistence
   boundary for preceding completed writes.
4. Actual survival of physical power loss also depends on the backend and the
   underlying storage system satisfying their persistence contracts.

The driver cannot manufacture persistent storage behind an in-memory backend.
The guest qualification here exercises QEMU, a host file, real WRITE and FLUSH
requests, orderly process exit and a fresh process reopening that file. It does
not simulate host power loss or prove a physical drive's power-failure behaviour.

Primary protocol reference: [VirtIO 1.2, section 5.2, especially 5.2.5 and
5.2.6](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).

## Publication outcome is part of the API

WRITE and FLUSH require an independent, correctly aligned
`UmicomKernelBlockMutationOutcome` output object. Callers must check the returned
status as well as that output. An invalid or inaccessible output object cannot
be repaired by attempting to write an error value through it.

| Outcome | What the caller has established |
|---|---|
| `UMICOM_BLOCK_NOT_SUBMITTED` | This admitted attempt did not make a new mutation request available to the device. |
| `UMICOM_BLOCK_SUBMITTED_UNCONFIRMED` | The request was published, but the complete successful result was not validated. A WRITE may already have changed some or all requested bytes. |
| `UMICOM_BLOCK_COMPLETED` | The request has a fully validated successful completion. A WRITE still needs a later successful FLUSH for the negotiated persistence boundary. |

For a well-owned admitted operation, the initial outcome is NOT_SUBMITTED. The
driver changes it to SUBMITTED_UNCONFIRMED before publishing the available-ring
index. Waiting until the notification register is written would be too late:
the index already makes the descriptor chain available to the device. Only a
fully checked successful completion changes the outcome to COMPLETED.

A timeout, device IO_ERROR, unsupported result or malformed response after
publication cannot promise unchanged media. The driver never retries a WRITE
automatically. A caller must retain its outcome and decide recovery with
knowledge of the operation's higher-level meaning. Reading the range back can
provide more evidence about current bytes; it does not by itself establish a
successful flush or an atomic transaction.

A valid device error completion consumes the chain and can leave the lease
READY for an explicit later operation, including FLUSH. Transport, ownership,
clock, configuration and completion-protocol failures use the existing fault
and reset path. Neither path converts an unconfirmed WRITE into a known success.

## Dirty and uncertain state have different meanings

The slot and `UmicomKernelBlockInfo` append three diagnostic fields:

| Field | Meaning |
|---|---|
| `writable` | The selected mode of the current or most recently claimed lease. WRITE and FLUSH also require successful admission and the READY state. |
| `needsFlush` | A WRITE has been published since the last fully successful FLUSH on this lease. |
| `writeUncertain` | At least one published WRITE on this lease lacked a fully validated successful result. This observation is sticky for that lease. |

A successful WRITE leaves `needsFlush` set. READ does not clear it. A failed
FLUSH does not clear it. A fully successful FLUSH clears `needsFlush`, but it
does not clear `writeUncertain`: flushing a partially completed write cannot
prove that every originally requested byte was written.

A failed writable admission can retain a claimed lease for cleanup with
`writable` set. This flag records the selected admission mode; it does not prove
that feature negotiation and queue setup succeeded. The live lease's mode is
fixed, and operations independently check its READY state.

For example, a device can change part of a range and then report IO_ERROR. The
outcome remains SUBMITTED_UNCONFIRMED and both diagnostic flags are set. If an
explicit later FLUSH succeeds, `needsFlush` becomes false while `writeUncertain`
remains true. The resulting bytes may now have crossed a persistence boundary;
the original requested content is still unconfirmed.

Close never submits an implicit FLUSH. Its success proves resource cleanup,
not durability. Diagnostic writer state remains observable until a subsequent
admission starts a new lease. A new lease's clear flags describe that new
lifetime; they do not erase the caller's responsibility to remember an earlier
uncertain result. Reset acknowledgement proves that the device has stopped
using the released queue memory, not that it has completed a disk mutation.

## Input, outcome and DMA ownership

WRITE accepts a stable trusted input span whose declared extent covers the
requested sectors. Its upper request bound is one 4 KiB bounce frame. Capacity
checking uses subtraction after validating the start sector, avoiding an
overflowing unchecked end-sector addition.

The entire declared input span must be disjoint from the domain, every retained
queue and bounce frame in that domain, and the mutation outcome object. The
outcome object has its own alignment and address-span checks and must also be
disjoint from every retained DMA frame. This includes pages held by a different
slot whose earlier reset or cleanup has not completed. Invalid output ownership
leaves the object untouched.

After preflight, the driver stages the requested bytes into its own bounce
frame before making the request available. It never lends the caller's input
buffer or outcome object to the device. The caller retains its storage through
the synchronous call; the driver does not alter the input.

| Request | Descriptor chain | Successful used length |
|---|---|---:|
| READ | Device-readable request header; device-writable bounce data; device-writable status | Requested data bytes plus one status byte |
| WRITE | Device-readable request header; device-readable staged data; device-writable status | One status byte |
| FLUSH | Device-readable request header; device-writable status; no data descriptor | One status byte |

The existing queue remains an eight-entry split ring with one outstanding
synchronous request. Publication and completion retain the platform's memory
and device-I/O barriers. The implementation checks the used-index transition,
chain identity, used length, written and defined status, live device state,
configuration and bounded clock observations before reporting success.

WRITE and FLUSH start their request clock before publication and include a
final deadline check even when completion was already visible at the first
poll. Each mutation observation must be no earlier than the preceding one.
A completion at or after the timeout boundary, or a backward clock, leaves the
mutation unconfirmed and faults the lease through the existing reset path.
The established READ timing behaviour remains unchanged.

A timeout does not permit scrubbing or freeing an exposed page. The ordinary
reset-acknowledgement and retryable release protocol remains responsible for
both DMA frames, including after a failed WRITE or FLUSH. This ownership model
assumes a compliant trusted device and the existing coherent-DMA platform.

## Source responsibilities

| Source | Responsibility |
|---|---|
| `include/umicom/kernel/virtio_block.h` | Writable admission, public mutation operations, outcome and appended diagnostics |
| `include/umicom/kernel/virtio_block_protocol.h` | Named WRITE, FLUSH and feature constants |
| `kernel/virtio_block.c` | Shared admission/request implementation, staging, outcomes, completion and retained ownership |
| `tests/virtio_block_write/` | Failure injection against the actual driver with separately modelled visible and flushed bytes |
| `tests/virtio_block/virtio_block_tests.c` | Existing read-only model with optional, zero-default extension hooks |
| `tests/virtio_block/writable_fixture_format.h` | Deterministic changed ranges and expected bytes for the disposable guest disk |
| `kernel/writable_block_boot.c`, `kernel/writable_block_validation.c` | Dedicated writer and fresh-process readback qualification images |
| `include/umicom/kernel/writable_block_validation.h` | Dedicated qualification entry declarations |
| `cmake/WritableBlock.cmake` | Build integration and the automatically prepared, ordered and cleaned CTest disk lifecycle |

All existing status numeric values and original member ordering remain intact.
Appending fields changes public structure sizes, so the Kernel and its clients
must be rebuilt together. Original source lines, explanatory comments, author
credits and licence headers remain present. Superseded implementation lines are
retained in explained disabled blocks; the active implementation shares the
original ownership and request machinery.

## Disposable guest disk lifecycle

The existing source fixture remains a 65,536-byte, 128-sector raw test disk.
Its SHA-256 remains:

```text
e5bea1290b3be59bdaf9f3a99d0be7527baefb3822b7d4b4e1c6c9736a4efa46
```

CTest prepares a separate `fixtures/umicom-writable-block.raw` in the selected
build directory. The writer attaches only that generated copy with
`readonly=off,cache=writeback`; the original source and existing read-only copy
are never writable attachments. The readback test starts a separate QEMU process
and attaches the same generated copy read-only. Fixture cleanup removes the copy.

The writer requires exactly one qualified modern block device with the expected
capacity and checks every original byte before the first WRITE. Fifteen WRITE
requests change twenty-two sectors: sector 2, the eight-sector range 7–14,
twelve individual sectors 16–27, and the final sector 127. The shared format
guarantees that each byte in these ranges differs from the original. Every
other sector must retain its original bytes.

Qualification covers a rejected out-of-capacity WRITE, guarded immutable input,
one-sector and full-page requests, repeated queue reuse, explicit dirty state,
successful FLUSH, complete disk comparison, reset, a fresh handle and repeated
readback. The separate read-only guest checks the persisted changes and every
untouched byte and verifies local WRITE/FLUSH refusal. Both restore owned frame
counts and the observed machine-control state before reporting success.

The QEMU processes use normal host flushing with explicit writeback caching.
They do not use `snapshot=on` or `-snapshot`: QEMU documents that snapshot mode
uses unsafe caching, which would undermine a test of the negotiated FLUSH path.
See [QEMU invocation, block device and cache options](https://www.qemu.org/docs/master/system/invocation.html).

The CTest fixtures enforce the order: current-image build, copy preparation,
successful writer, fresh readback, cleanup. A filtered readback run automatically
includes its prerequisites. A failed build or writer prevents the dependent
readback test from earning a pass, and fixture cleanup still owns removal of the
generated copy. Every stage has a finite timeout.

Preparation and both guests require the current-image build fixture. Cleanup
deliberately has no successful-build prerequisite: it must also remove a copy
left by an earlier interrupted run when the next build fails. The checked
failure case therefore blocks both guests while still running cleanup.

## Build and run on Windows

Merge the complete supplied files into the existing checkout, then use the
established configured toolchain from PowerShell:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Stop if any command fails. To rerun the complete writable-disk lifecycle through
the readback test's automatic fixture dependencies:

```powershell
ctest --preset riscv64-clang-debug -R "kernel\.riscv64\.block_write_readback$" --output-on-failure --no-tests=error
```

The writer must report `UMICOM_KERNEL_WRITABLE_BLOCK_READY`; the second guest
must report `UMICOM_KERNEL_BLOCK_WRITE_READBACK_READY`. The ordinary full suite
also exercises the earlier read-only transport, FAT16 inspector and mounted
filesystem acceptance paths.

The optional native failure-injection suite is a separate CMake project in
`tests/virtio_block_write`. Its allocation/release injection uses an ELF host
linker's wrapping support. The verified native configuration is Linux; those
commands are not presented as a native Windows linker qualification. Detailed
executed results and integration commands accompany the validation guide and
delivery review.

## Work that remains above this transport

A writable filesystem needs its own mutation rules, allocation and metadata
ordering, ownership of dirty buffers, error propagation and recovery protocol.
That work must decide which operations can leave partial state and how to
inspect or recover it after interrupted writes. This driver deliberately makes
uncertainty visible so that later layers can make those decisions correctly.

This addition does not grant FAT16 mutation, create or delete disk files, format
media, add a filesystem journal, provide atomic multi-sector updates or expose
a general disk-writing console command. It also retains the existing single-
hart, polling and fixed-platform limits. Successful transport qualification is
the prerequisite for that next layer, not a writable-filesystem release claim.
