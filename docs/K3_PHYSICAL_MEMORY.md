# K3 — Umicom Kernel Physical Memory Foundation

Status: source delivery / target qualification required on the developer's QEMU host  
Target: QEMU RISC-V 64 `virt`, 128 MiB RAM  
Implementation: C23; existing K1/K2 RISC-V Assembly preserved  
Virtual memory/page tables: **not implemented in K3**

## 1. What K3 adds

K3 is the first milestone that lets Umicom Kernel reason about ownership of real
physical RAM pages.

The selected QEMU profile is:

```text
RAM base   0x80000000
RAM bytes  128 MiB
Page size  4 KiB
Frames     32768
```

The physical allocator never returns a page that belongs to:

- the low RAM/bootstrap area below the linked Kernel;
- the complete linked Kernel image, including BSS and bootstrap stack;
- the allocator's own static bitmaps, because they live inside Kernel BSS;
- the validated QEMU device-tree blob (DTB);
- a caller's existing allocation;
- an explicit reservation.

## 2. This is not virtual memory

K3 manages physical frame addresses only.

There are no RISC-V page tables, `satp`, address spaces, process mappings or
page faults yet.

At this milestone the Kernel executes with physical addresses directly visible.
The later virtual-memory milestone will consume K3 frames rather than replacing
the physical allocator.

## 3. Why 4 KiB pages

K3 selects:

```text
4096 bytes = 4 KiB
```

as one physical frame.

This is a conventional granularity for RISC-V page-based virtual memory and is
small enough for ordinary Kernel/user mappings later.  K3 records the choice in
one public constant:

```c
UMICOM_KERNEL_PAGE_SIZE
```

Changing it later is an architectural change, not a cosmetic constant edit.

## 4. Why two bitmaps

A single "used" bit cannot explain *why* a frame is unavailable.

K3 therefore uses two independent one-bit-per-frame maps:

```text
reserved bitmap    Kernel/platform owns the page
allocated bitmap   an AllocateFrame caller owns the page
```

For 65,536 maximum representable frames, each bitmap is 8 KiB.  Together the
first allocator spends 16 KiB of static BSS metadata.

The current 128 MiB QEMU profile needs only 32,768 active frame bits, but the
metadata capacity leaves room up to 256 MiB without changing the structure.

A frame is free only when both bits are zero.

The two bits are never permitted to be set simultaneously.

## 5. Why the metadata is static

An allocator has a bootstrapping problem: if it needs dynamically allocated
memory in order to manage dynamic memory, something else must allocate the
metadata first.

K3 avoids that circular dependency by placing its bitmaps in ordinary Kernel
BSS.

The linker includes BSS before `__kernel_end`, and K3 reserves RAM from the
platform RAM base through `__kernel_end` before making ordinary allocations.
Therefore the allocator cannot accidentally return its own metadata pages.

Later milestones may introduce scalable boot allocators or dynamic metadata,
but this bounded implementation is deliberately inspectable and safe for the
first physical-memory stage.

## 6. Kernel range protection

The linker exports:

```text
__kernel_start
__kernel_end
```

K3 reports both addresses during boot.

The allocator reserves from:

```text
RAM base
```

through:

```text
__kernel_end
```

rather than starting only at `__kernel_start`.

That intentionally protects the 2 MiB low-RAM gap below the current
`0x80200000` link address as well as the complete Kernel image.

No ordinary caller can allocate those pages.

## 7. Device-tree protection

QEMU passes a DTB address to the boot hart in RISC-V register `a1`.

K1 and K2 printed the address but did not read the blob.

K3 reads only two standard big-endian header fields:

```text
offset 0: magic     0xd00dfeed
offset 4: totalsize
```

It validates:

- the first required header bytes are inside configured RAM;
- the magic is correct;
- totalsize is at least the fixed header size;
- `address + totalsize` cannot overflow;
- the complete DTB remains inside configured RAM.

Only after those checks does K3 reserve every physical page touched by the DTB.

K3 does **not** parse device nodes or properties yet.  Reading the memory node
from the DTB is later work; the current QEMU platform adapter deliberately owns
the fixed `-m 128M` profile.

## 8. Checked address arithmetic

`kernel/address.c` introduces reusable helpers for:

```text
checked address addition
alignment down
checked alignment up
power-of-two validation
```

The memory manager never relies on unsigned wrap-around.

For example:

```text
maximum-address + 1
```

must be rejected rather than becoming zero.

## 9. Allocation policy

K3 uses deterministic first-fit allocation:

1. inspect frame 0;
2. skip it if reserved or allocated;
3. continue upward;
4. return the first completely free page.

This is not a claim that first-fit is the final high-performance allocator.
It is intentionally predictable for tests, teaching and early debugging.

A later allocator can add free lists, zones, NUMA or larger-page strategies
behind reviewed contracts.

## 10. Failure-atomic reservations

`UmicomKernelPhysicalMemoryReserveRange()` uses two passes.

First pass:

```text
validate every frame is free
```

Second pass:

```text
set every reservation bit
```

If the last page overlaps an existing owner, the first pages are **not** left
partly reserved.

Release uses the same two-phase idea: prove the complete range is reserved,
then clear it.

## 11. K3 runtime self-test

The real QEMU run performs all of these checks:

1. validate RAM profile;
2. validate DTB header/range;
3. initialize allocator;
4. reserve low-RAM + Kernel;
5. reserve DTB;
6. validate baseline bitmap accounting;
7. refuse address overflow;
8. allocate frame A;
9. allocate distinct frame B;
10. verify A and B are page aligned;
11. free A;
12. reserve A;
13. refuse freeing reserved A;
14. refuse overlapping reservation of A;
15. release A reservation;
16. refuse releasing it again;
17. allocate frame C and prove deterministic reuse of A;
18. free C;
19. refuse double-free of C;
20. refuse misaligned free;
21. free B;
22. refuse reservation outside RAM;
23. refuse a new reservation overlapping the Kernel;
24. recount every active bitmap bit;
25. prove temporary allocation count returned to zero;
26. prove reserved/free accounting returned to its baseline.

Only then does the image print:

```text
memory-test=pass
state=physical-memory-ready
K3_PHYSICAL_MEMORY_PASS
```

## 12. Full-name Umicom convention

K3 establishes full names for new Kernel contracts:

```c
UmicomKernelPhysicalMemoryInitialize(...)
UmicomKernelPhysicalMemoryAllocateFrame(...)
UmicomKernelAddressAddChecked(...)
UmicomKernelDeviceTreeInspect(...)
UmicomPlatformPhysicalMemoryDescribe(...)
```

K1/K2 already committed functions such as `UmiKernelConsoleWrite()` and
`UmiRiscvTrapInstall()`.

Those historical symbols are not silently removed.  Headers provide full-name
source aliases such as:

```c
UmicomKernelConsoleWrite(...)
UmicomRiscvTrapInstall(...)
```

which currently preserve the original binary symbols.

A later stable native ABI design may export full-name symbols with compatibility
wrappers.  K3 does not invent that ABI prematurely.

## 13. What K3 does not prove

A K3 pass does not prove:

- virtual memory;
- page-table correctness;
- user/kernel isolation;
- multiple RAM banks;
- NUMA;
- physical hardware discovery;
- hot-plug memory;
- SMP allocator concurrency;
- cache coherency;
- DMA/IOMMU behaviour.

Those remain explicit later milestones.

## 14. Next direction

The roadmap originally placed threads/scheduling after physical memory.  Before
K4 scheduling, the next implementation should review whether a small page-table
foundation is needed for safe per-thread/kernel-stack work, while preserving the
roadmap's rule that K3 itself remains purely physical memory.
