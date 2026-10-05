# Umicom Kernel — Checked object caches and frame observation

## Why this is the next allocation layer

A physical-frame allocator owns whole pages. A service record, directory entry or
queued work item often needs much less space. This layer groups equal-sized
Kernel records into independently allocated physical frames while keeping their
lifetimes explicit.

It does not replace the physical allocator or move existing scheduler, process,
IPC or event objects. Those services retain their current storage and ownership
contracts. A later service can choose a cache when its lifetime and alignment
requirements are clear.

The release roadmap groups allocation, stronger VM ownership and protected
stack mappings in one work package. This implementation covers **small-object
allocation and read-only frame classification**. It does not complete that whole
package: mapped regions, page-table owner identities and unmapped stack guards
still need separate implementation and acceptance.

## A cache has one geometry

At initialisation, the caller chooses an object size, alignment and frame quota.
There is no variable-sized `malloc` interface hidden underneath it.

| Property | Supported contract |
|---|---|
| Object size | 1 through 4,080 bytes |
| Alignment | A power of two from 1 through 4,096 bytes |
| Frame quota | 1 through 16 independently allocated frames |
| Tail guard | At least 16 bytes after each object |
| Metadata | Stable caller-owned storage, outside the object frames |
| Execution | Trusted, serial calls on hart zero, with the documented machine state |

The stride is the object size plus its guard, rounded up to at least sixteen-byte
alignment. Unused trailing bytes in a page are not treated as another slot.

For example, a 64-byte object aligned to 64 bytes has a 128-byte stride. One
4 KiB frame holds 32 objects; the other 64 bytes in each slot provide its guard
and alignment padding. A 4,080-byte object aligned to a page uses one slot and
leaves sixteen bytes for the guard.

This trades memory efficiency for understandable alignment and diagnostic
behaviour. The current service is deliberately bounded and performs exhaustive
metadata/guard checks; it is not a constant-time or tuned production slab
allocator.

## Ownership is layered, not duplicated

```text
Kernel component
    owns the cache metadata for the cache's entire lifetime
                     |
                     v
Object cache
    owns allocated physical frames and records each live object ticket
                     |
                     v
Existing physical allocator
    owns the frame bitmap and whole-frame allocation/release decisions
```

Initialisation acquires no frame. The first allocation requests one from the
existing physical allocator. Later objects share that page until its slots are
full. Another frame is acquired only when required and within the quota.

The next frame need not be adjacent. The guest validation deliberately inserts
an unrelated allocated frame between the cache's first and second pages. The
cache must still resolve every object's address and preserve its contents.

Metadata has fixed upper-bound storage for sixteen page records and 128 possible
slot tickets per record. That remains a static cost even for a smaller configured
quota. Actual object data is dynamically backed. Metadata placed inside Kernel
BSS is protected by the existing image reservation; a caller using some other
location must keep it stable, non-overlapping and reserved/owned appropriately.

## A reference is more than an address

`UmicomKernelObjectReference` contains its cache domain, object address and a
64-bit allocation ticket. A successful allocation issues a new ticket. Reusing
an address does not reuse its old ticket. The maximum ticket can be issued once;
subsequent allocations refuse rather than wrap.

`Resolve` checks the complete reference before returning the object pointer.
`Free` checks the same reference before clearing or releasing the slot. Null,
foreign-cache, interior-address, wrong-ticket and already-freed references do not
free an object. There is no pointer-only free operation.

These are Kernel bookkeeping references, **not cryptographic capabilities**.
Copying a reference copies authority to the same live allocation; it does not
add a reference count. The allocating component must coordinate all consumers
before freeing the object. A raw C pointer remains capable of writing memory
and is invalid for further use after successful Free. The ticket check protects
allocator operations; it does not turn a stale pointer dereference into a trap.

Cache identity is local to its stable lifetime. Do not copy, reset or reinitialise
a live or closed cache. Reusing its metadata address as a new zero-filled cache
while old references survive is outside the contract.

## Clearing and diagnostic guards

A new physical frame is completely cleared before use. Allocation clears its
selected slot, fills the tail guard and only then publishes its ticket and
reference. The caller receives zeroed object bytes, but must still establish any
higher-level invariants its record needs.

Free checks the live guards, clears the whole slot (including padding), then
marks that ticket free. Volatile stores keep the scrub from being optimised away
at the end of the allocation's lifetime. Empty object memory never contains
free-list links, so an old pointer cannot rewrite a next-free pointer there.

The guard is a ticket-dependent diagnostic byte pattern. It detects writes into
the checked tail; it is not an inaccessible page, a memory-protection boundary
or a guarantee that every overrun/underrun is found. A wild write can skip it or
corrupt other Kernel memory. No new page-table permission change is made here.

If a check detects corruption, the cache becomes POISONED. Addresses, tickets,
counts and frame ownership are retained for diagnosis. Allocation, free and
close then refuse. There is no automatic force-free or unpoison operation.
Destructive guard tests therefore run in isolated native test processes, not as
an ordinary guest test which would claim to restore all of its frames.

## Read-only physical-frame classification

`UmicomKernelPhysicalMemoryFrameQuery()` checks an aligned address through the
existing allocator's range rules and private bitmaps. It returns FREE, RESERVED
or ALLOCATED without changing allocation state. Its output remains untouched on
error; simultaneous reserved/allocated bits report an invariant failure.

The cache uses this query before reading the tail guards of a backing page. If
the page is no longer allocated, it refuses rather than following its bytes.

**ALLOCATED does not identify who owns the frame.** It cannot detect another
component wrongly freeing a cache page and reallocating that address before the
next check. Existing exclusive frame-ownership rules remain mandatory. This
query is not a generation-tagged physical-frame lease, and the existing page
walker is not silently upgraded into one.

## Free, trim and close are different operations

`Free` ends an object's lifetime but keeps its now-empty backing page in the
cache. This avoids a whole-frame allocation for every short-lived record.

`Trim` releases only empty pages. Live pages are preserved. It scrubs a page and
asks the original physical allocator to release it; only a successful release
clears that page's metadata and decreases the owned-frame count.

If a release is refused, Trim stops. The output count reports the pages actually
released so far; the failed page and all later pages remain owned. Retrying does
not release successful earlier pages again. An ordinary failed Trim leaves an
open cache usable when its ownership records remain valid.

`Close` first refuses any live object. Once it can proceed, it moves the cache to
CLOSING before trimming, freezing admission. A failed close leaves that state
and its outstanding frames intact for retry. Complete success makes the cache
CLOSED. Closing again is harmless, but reopening/resetting it is not supported.

```text
OPEN -- live objects --> Close refuses, still OPEN
  |
  +-- no live objects --> CLOSING -- complete release --> CLOSED
                             |
                             +-- release refused --> retain remaining frames
                                                      retry Close
```

## Machine and concurrency boundary

The RV64 adapter checks hart zero, Bare translation, no enabled interrupt sources,
MIE clear and MPRV clear. It also consults the existing interrupt-ownership
predicate so an unfinished managed section or timer lease refuses allocation.
The old ownership implementation is not replaced.

Normal calls must be serial Kernel operations with valid, non-overlapping Kernel
pointers. The service does not introduce an SMP lock, disable pre-emption for a
caller or allocate from an interrupt handler. Quiescent cooperative callbacks
may use it only while meeting the same contract; they must retain a live object
for as long as another callback or task borrows its pointer.

All errors leave outputs unchanged except Trim's documented completed-release
count after a release failure. Snapshot is a checked value read, not a diagnostic
bypass into a poisoned cache; use a debugger to inspect retained poisoned records.

## A work-record example

The following is a service-side pattern, not a change to any existing service:

```c
typedef struct UmicomKernelWorkRecord {
    UmicomU64 request;
    UmicomU64 progress;
} UmicomKernelWorkRecord;

static UmicomKernelObjectCache umicomWorkCache;

/* During service setup, once. */
UmicomKernelObjectStatus status = UmicomKernelObjectCacheInitialize(
    &umicomWorkCache, sizeof(UmicomKernelWorkRecord),
    alignof(UmicomKernelWorkRecord), 4U);

/* After checking setup succeeded, obtain one owned record. */
UmicomKernelObjectReference reference = {0};
status = UmicomKernelObjectCacheAllocate(&umicomWorkCache, &reference);
if (status == UMICOM_OBJECT_OK) {
    void *storage = (void *)0;
    status = UmicomKernelObjectCacheResolve(&umicomWorkCache, reference, &storage);
    if (status == UMICOM_OBJECT_OK) {
        UmicomKernelWorkRecord *work = (UmicomKernelWorkRecord *)storage;
        work->request = 100U;
        work->progress = 0U;
        /* Use the record here. No consumer may keep storage after Free. */
    }
    /* A caller must handle a refused Free; it cannot assume memory disappeared. */
    status = UmicomKernelObjectCacheFree(&umicomWorkCache, reference);
}
```

During service shutdown, finish or cancel every borrower, free all live records,
and call Close. Do not interpret a BUSY close as permission to erase metadata.

## Normal Windows workflow

Use the normal incremental configure, build and CTest commands. The additional
guest test is `kernel.riscv64.object_caches`, with the existing current-image
fixture. No new tool installation or build-directory removal is needed.

The guest uses forty 64-byte records across two non-contiguous cache frames,
checks stale and wrong-domain references, trims and reuses pages, exercises a
page-aligned maximum-sized object and refuses allocation during a critical
section. It then restores its physical-frame accounting and checks the original
ECALL handler.

## Optional native checks

On a host with a C23-capable compiler and CMake/Ninja:

```text
cmake -S tests/object_cache -B build/object-cache-native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/object-cache-native --parallel 2
ctest --test-dir build/object-cache-native --output-on-failure --no-tests=error
```

On a Linux host where the compiler has its sanitizer runtime available, add
`-DUMICOM_OBJECT_SANITIZERS=ON` at configuration. This is not a new requirement
for the normal Windows-to-RISC-V build. Native checks model the architecture
gate and include deliberate release refusal; they do not execute RV64 CSRs.

The additional `guest-sequence-model` test runs the exact guest C acceptance
sequence with explicitly modelled hardware observations and real allocators.
It is useful orchestration evidence, not a QEMU boot result.
