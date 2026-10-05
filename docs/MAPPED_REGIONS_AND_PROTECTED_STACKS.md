# Umicom Kernel — Owned mapped regions and protected stacks

## A mapping and an allocation have different owners

The physical allocator answers which whole RAM frames are available. The
existing virtual-memory service builds page tables, but deliberately does not
own the data pages named by their leaves. That separation is useful, provided
some higher owner coordinates their lifetimes.

`UmicomKernelMappedSpace` is that higher owner for a bounded, immutable layout.
It owns data frames, records the table frames created by the existing mapper,
and will not destroy an address space while an execution lease is outstanding.
It does not replace the mapper, object cache, loader or process owner.

The new protected-stack demonstration uses the existing supervisor entry and
return path. It neither introduces another trap handler nor migrates the
existing machine trap, cooperative-thread or process stacks.

## The lifecycle is intentionally visible

```text
Zero-filled stable owner
          |
       Initialise
          |
       PLANNING -- add region/stack descriptions; no frames allocated
          |
        Build -- allocate data, zero/copy bytes, create/map/audit tables
          |
        READY -- immutable layout; checked Kernel data reads/writes
          |
        Borrow -- issue a root and one-use execution ticket
          |
       BORROWED -- no layout changes, writes or teardown
          |
        Return -- synchronous caller has stopped using the root
          |
        READY
          |
        Close -- destroy page tables, then scrub/free data
          |
        CLOSED -- no reinitialisation or token resurrection
```

An ordinary failed build releases its partial allocations and returns to
PLANNING. The same plan can be retried after memory becomes available. If a
cleanup operation itself is refused, the owner stays CLOSING so `Close` can
retry the remaining work. Corrupt provenance instead poisons the owner: no
automatic force-free is attempted.

This is not an in-place region editor. There is no live remap, growth, shrink,
permission-changing operation, sharing alias, or demand-paging callback. Once
built, the layout is immutable. Those operations need their own ownership and
TLB policies rather than being hidden in this first owner.

## Describing a region

A `UmicomKernelMappedRegionSpec` contains the first **data** address, page count,
permissions, optional lower/upper guard flags and optional initial bytes.

Each guard reserves one whole page in the virtual interval. It has no physical
backing and is not mapped. A later plan cannot occupy another region's guard,
even though the guard has no present PTE.

For a two-page stack beginning at address `B`:

```text
B - 4096            lower guard: no leaf and no backing frame
B                   first writable data page
B + 4096            second writable data page
B + 8192            exclusive stack top / start of upper guard
B + 12288           exclusive end of the reservation
```

`UmicomKernelMappedStackAdd()` constructs that policy: READ + WRITE, no EXECUTE,
a guard on each side, and optional USER permission. A stack pointer starts at
its exclusive data end and grows downwards. The initial subtraction and store
therefore land in the last data page, not in the upper guard.

The general region API accepts readable, readable/writable or readable/executable
RAM, optionally USER. It rejects write-only, execute-only, global, unknown and
simultaneously writable/executable permissions. The underlying mapper supports
a wider interface; this owner's narrower policy is intentional.

| Bound | Contract |
|---|---|
| Regions | At most eight append-only plans per owner |
| Data per region | One through sixteen 4 KiB pages |
| Total data | At most sixty-four pages per owner |
| Table ledger | At most 129 entries, bounding the scattered-page worst case |
| Guards | Independently optional lower/upper pages; both required by StackAdd |
| Address range | One canonical Sv39 half, page aligned, no endpoint wrap |
| Null page | Cannot be a data region; it may remain an unmapped lower guard |
| Execution | Trusted serial Kernel calls on hart zero under the allocation gate |

The final virtual page whose exclusive end would wrap to zero is not supported.
The owner has fixed metadata storage even when the selected layout is smaller.

## Initial data is copied before publication

A region specification is copied into the owner, but its `initialData` byte
span is borrowed until Build succeeds or the plan is closed. Keep that span
immutable, valid and separate from the owner and newly allocated backing.

Build clears **every byte of each acquired data frame**, copies the specified
prefix and leaves the rest zero. This includes page padding, not just a program's
requested BSS bytes. The source need not survive successful publication: its
pointer is cleared from the owner and runtime memory uses private copies.

Code bytes are copied while the root is unpublished, then mapped READ + EXECUTE.
The checked write API refuses to change that region later. Before issuing an
execution lease, the architecture adapter reuses the loader's existing local
`FENCE.I` primitive. Instruction visibility is not provided by a page-table
fence alone. This is a single-hart operation, not remote instruction-cache
synchronisation.

## Non-contiguous physical backing is normal

Every data page comes from a separate call to the established physical allocator.
Adjacent virtual pages can therefore name non-adjacent physical frames. The
owner's checked read/write helpers split the copy across those recorded pages.

Read and Write preflight the complete byte range and all backing ownership
before copying. Writes require the region's WRITE permission even though the
caller is in machine mode. Buffers cannot overlap owner metadata, data backing
or live table frames. A zero-length read at the exclusive end performs no access.

These are trusted **Kernel-buffer** APIs. They are not user-copy syscalls and do
not make an arbitrary inaccessible Kernel pointer safe. User input must first
cross the existing checked user-memory boundary.

## Table provenance is checked before following a pointer

The established mapper is still the only implementation that creates and destroys
PTEs. The new audit is an independent reader with a different job: verify that
the links it is about to follow are links this owner actually acquired.

Immediately after its own trusted Create/Map call, during unpublished Build,
the owner records reachable table addresses, their level and their virtual
prefix. No mapper or interrupt runs concurrently in that interval. Ordinary
Validate, Borrow and Close do not add addresses to this ledger.

A public audit checks ledger membership, live allocation classification, level,
prefix and visitation before reading a child table. A pointer redirected into
UART space or an unrelated allocated page is refused before dereference. A
cycle or a second link to one of this owner's tables is refused too. Every
leaf must match the planned virtual address, exact backing page and permissions.
A leaf in a reserved guard is corruption, not a newly discovered region.

The walk is bounded to three levels and the ledger quota. Its leaf/table totals
must agree with the existing hierarchy counters. Released table records are
marked non-live only after the existing destructor's controlled release call.
A partially destroyed hierarchy can then be audited without resurrecting freed
tables or expecting already-removed leaves to remain.

This is **not** a retroactive provenance audit for every root in Umicom Kernel.
Existing process and supervisor owners are untouched. Their future migration
must be explicit, not achieved by making their old APIs silently call this one.

The physical query's ALLOCATED result does not identify an owner or an allocation
generation. If another Kernel component wrongly frees and reuses a frame behind
this owner's back, classification alone cannot establish its original lifetime.
Ledger metadata is trusted Kernel state, not cryptographic evidence against
arbitrary machine-mode writes. These are remaining system-wide ownership rules.

## Borrowing and quiescence

Borrow returns `{ owner, ticket, root }` and permits one synchronous consumer.
A second borrow and Close both fail until Return. Return checks the domain,
ticket and root, so double-return and a previous lease do not release a later
borrow. The maximum ticket is used once; exhaustion does not wrap.

The caller must ensure the root is no longer used when returning it. The
architecture gate confirms the supported machine context (including Bare satp),
but cannot discover a saved pointer in arbitrary Kernel code or another hart.
This is not a refcounted multiprocess address space or an SMP TLB shootdown.

Mutation reuses the existing object-cache allocation gate rather than creating
a second interrupt controller. Outstanding managed critical sections and timer
ownership are refused. The gate is an admission check, not a lock; Kernel code
must not yield or access the same owner concurrently while performing a call.

Region handles are append-only slot identifiers scoped to their owner. They are
not process-registry tokens. A closed owner is not reset at the same address;
reinitialising it could give old references meaning again.

## The rollback repair included here

A new double-failure test first refuses allocation of the next intermediate
table and then refuses the rollback release of its freshly created parent.
The old MapPage path disconnected that parent and ignored the release error.
That left a physical allocation without a reachable ownership record.

The targeted insertion now attempts release **before** disconnection. On refusal,
the empty parent stays linked and counted, and the caller receives an explicit
physical-memory error. The mapped owner captures that still-owned table and the
existing destructor can finish the cleanup. The original unchecked release
statement remains in an explained `#if 0` block; no original line is deleted.

This repair is limited to the intermediate-table creation rollback exercised
here. It is not a claim that every old mapper failure path has been redesigned.
The mapped-space owner never calls the older incremental Unmap API: immutable
layouts are destroyed through the complete hierarchy destructor.

## Real protected-stack acceptance

The new Kernel test builds three regions: private RX probe text, a two-page RW
stack with two unmapped guards, and a writable observation page. Text bytes are
copied into independently owned frames and retain their linked virtual addresses.
The original machine code pages are not exposed to the supervisor payload.

Six cases use the existing supervisor monitor:

| Case | Hardware observation required |
|---|---|
| Nested stack return | Supervisor ECALL 9, exact completion PC/cookie, nested C result 160 |
| Lower guard load | Load page fault 13 at the exact probe and lower guard address |
| Upper guard store | Store page fault 15 at the exact probe and exclusive stack top |
| Downward stack overflow | Actual sp decrements/stores reach B-16; cause 15 and interrupted sp/value both identify that address |
| Non-executable stack | Instruction page fault 12 at the RW stack address |
| Return after faults | A fresh invocation on the same region succeeds with result 160 |

Each case requires the recorded previous privilege to be supervisor, the
observed satp to name the borrowed root and the ordinary machine control state
to be restored. Closing while borrowed must fail. After all six, the existing
machine ECALL handler must still work and physical accounting must return to
its original baseline.

Guard pages affect translated accesses. **They do not protect ordinary Bare
machine-mode stacks** whose accesses bypass this hierarchy. This delivery does
not replace the existing cooperative-stack canaries or claim that every Kernel
stack is now protected by page faults.

## Normal Windows workflow

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run the same `virt,aclint=off`, `-m 128M`, `-smp 1`, `-bios` command used for the
existing Kernel. The new marker is `UMICOM_KERNEL_MAPPED_REGIONS_READY`. The
suite adds `kernel.riscv64.mapped_regions` with the existing current-image fixture.
There is no build-directory deletion, installer or additional host dependency.

## Optional native tests

On a host with Clang and its native sanitiser runtimes:

```text
cmake -S tests/mapped_regions -B build/native-mapped-regions -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_MAPPED_SANITIZERS=ON
cmake --build build/native-mapped-regions --parallel 2
ctest --test-dir build/native-mapped-regions --output-on-failure --no-tests=error
```

The suite uses real C ownership, page-table and physical allocation code in a
host arena. It models only architecture admission/instruction publication and
controlled backend allocation/release refusals. It does not enter RISC-V S-mode.
Read `MAPPED_REGIONS_VALIDATION.md` for the exact performed checks and limitations.

## Next work

The next planned implementation is the VFS and RAM-backed file foundation.
Mapped regions now provide a separate bounded ownership option; VFS adoption
must choose its data/metadata lifetimes explicitly. This does not mark general
virtual allocation, live region mutation, SMP ownership or all protected Kernel
stacks complete.

## Architecture references

* [RISC-V supervisor ISA](https://docs.riscv.org/reference/isa/priv/supervisor.html): Sv39 tables, permissions and page-fault behaviour.
* [RISC-V machine ISA](https://docs.riscv.org/reference/isa/priv/machine.html): effective privilege, MPRV and trap state.
* [RISC-V Zifencei](https://docs.riscv.org/reference/isa/unpriv/zifencei.html): local instruction-fetch synchronisation.
* [CTest fixture requirements](https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html): failed setup prevents dependent execution.
