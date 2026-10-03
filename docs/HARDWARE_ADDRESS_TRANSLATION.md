# Umicom Kernel — Hardware-Backed Sv39 Address Translation

## Why this step exists

The virtual-memory subsystem can already build an Sv39 hierarchy, walk it in
software, reject malformed mappings and return page-table frames to the physical
allocator.  That is necessary, but it is still possible for software and the
processor to disagree about a page-table encoding.

This capability therefore asks the RISC-V processor to perform a real page
walk.

The Kernel deliberately does not move its instruction stream or stack behind
virtual addresses yet.  Instead, it remains in machine mode and uses the RISC-V
`MPRV` mechanism for two tightly controlled data accesses:

1. read a known value through a virtual address;
2. write a new value through that same virtual address.

If the translated load returns the physical seed value, and the translated
store changes the underlying physical frame, the page-table encoding has been
validated by the hardware rather than only by our own software walker.

## Why MPRV is useful here

`mstatus.MPRV` changes the privilege used for explicit loads and stores while
the hart itself remains in machine mode.  `mstatus.MPP` selects the effective
privilege.

The validation sequence uses:

```text
current privilege:        Machine
MPRV:                     1
MPP:                      Supervisor
satp.MODE:                Sv39
```

The one controlled load/store therefore uses supervisor address translation,
but machine-mode instruction fetch remains unchanged.

That gives the project a useful safety boundary: a mistake in the new page
tables cannot immediately make the currently executing Kernel code, stack or
trap vector disappear from the CPU's view.

## Temporary PMP validation region

Supervisor-effective memory accesses are also subject to Physical Memory
Protection on implementations that provide PMP.

For this short validation path, machine mode creates one broad, **unlocked** TOR
region with read/write/execute permission.  It exists only to prevent an empty
PMP policy from obscuring what this test is meant to measure: Sv39 translation.

This is not the future Umicom Kernel security policy.

The validation code removes the PMP region immediately after the translated
accesses.  Future privilege separation will replace this broad temporary rule
with explicit regions and rights derived from the Kernel security architecture.

## satp construction

The root page table is a 4 KiB physical frame.

Sv39 `satp` contains:

```text
63        60 59                44 43                              0
+-----------+--------------------+---------------------------------+
| MODE = 8  | ASID = 0           | root physical page number       |
+-----------+--------------------+---------------------------------+
```

The root physical page number is:

```text
rootTablePhysicalAddress >> 12
```

because every page table is 4096-byte aligned.

After writing `satp`, the Kernel executes:

```text
sfence.vma
```

to discard stale address-translation state before the mapping is used.

## Trap safety while MPRV is active

A malformed mapping could cause the translated load or store to raise a page
fault.

That failure path must still be able to print a useful diagnostic.

The trap entry therefore preserves the original `mstatus` in the trap frame,
then clears **live** `MPRV` before it calls the C23 trap dispatcher.  The handler
can consequently access Kernel RAM and MMIO with normal machine-mode semantics
even if the translated access that triggered the trap was invalid.

For ordinary exceptions and timer interrupts, MPRV was already zero, so this
does not change their normal behaviour.

## Ownership

The same ownership rules established by the page-table subsystem remain in
force:

- the address space owns page-table frames;
- the test owns the mapped data frame;
- destroying the address space does not free the mapped data frame;
- the data frame is released separately;
- final physical-memory counters must return exactly to their starting values.

## Runtime evidence

The QEMU transcript should contain evidence similar to:

```text
hardware-translation-test=begin
hardware-translation.create=ok
hardware-translation.data-frame.allocate=ok
hardware-translation.map=ok
hardware-translation.validate=ok
hardware-translation.satp=0x8...
hardware-translation.load=0x1122334455667788
hardware-translation.physical-after-store=0xa5a55a5af0f00f0f
hardware-translation.unmap=ok
hardware-translation.destroy=ok
hardware-translation.data-frame.free=ok
hardware-translation-test=pass
state=hardware-translation-ready
UMICOM_KERNEL_HARDWARE_TRANSLATION_READY
```

The exact root page-table physical page number inside `satp` may vary because
the physical allocator decides which free frame is available.

## What this does not claim

This capability does **not** mean that:

- the Kernel executes in supervisor mode;
- Kernel instruction fetch uses virtual addresses;
- the Kernel stack uses virtual addresses;
- user mode exists;
- separate processes have separate address spaces;
- PMP is configured as a production security boundary.

Those changes deserve their own carefully reviewed execution transition.

The important result here is narrower and stronger: the CPU's real Sv39 walker
has successfully consumed an Umicom Kernel page table for both a load and a
store.
