# Umicom Kernel — RISC-V Sv39 Page-Table Foundation

## Purpose

This subsystem introduces virtual-address translation structures without enabling the processor's address-translation hardware yet.

That separation is intentional. The Kernel first proves that it can construct, inspect, modify and reclaim trustworthy page tables while still executing in machine mode with direct physical addressing. Supervisor/user execution can activate the resulting model later through `satp`.

## Sv39 address structure

A canonical Sv39 virtual address contains:

```text
63                              39 38        30 29        21 20        12 11       0
+--------------------------------+------------+------------+------------+-----------+
| sign extension of bit 38       | VPN[2]     | VPN[1]     | VPN[0]     | offset    |
+--------------------------------+------------+------------+------------+-----------+
                                  9 bits       9 bits       9 bits       12 bits
```

Each table is one 4 KiB physical frame containing 512 64-bit entries.

The current implementation supports only 4 KiB leaves at level zero. One-gigabyte and two-megabyte large-page leaves are rejected explicitly rather than being partially implemented.

## Ownership

`UmicomKernelVirtualAddressSpace` owns only its page-table frames.

A physical frame mapped as a leaf remains owned by the caller. Destroying an address space therefore returns page-table frames but does not free caller data frames.

This distinction prevents page-table lifetime from silently becoming physical-object lifetime.

## Mapping rules

The mapper validates:

- canonical Sv39 virtual addresses;
- 4 KiB alignment of virtual and physical page bases;
- permission bits;
- the RISC-V rule that writable leaf mappings must also be readable;
- duplicate mappings;
- unsupported large-page leaves;
- physical allocator failures.

New intermediate page tables are allocated transactionally. If a later stage of a mapping attempt fails, tables created only for that failed attempt are disconnected and returned to physical memory.

## Translation

`UmicomKernelVirtualMemoryTranslate()` walks all three levels in software and returns the mapped physical address including the original byte offset inside the page.

This is a validation tool, not the CPU MMU. No `satp` write occurs in this subsystem.

## Unmapping and reclamation

Removing the final leaf from a level-zero table reclaims that table. If its parent level-one table also becomes empty, that frame is reclaimed too. The root remains until the complete address space is destroyed.

## Acceptance evidence

The runtime validation proves:

- root table creation;
- allocation of the two intermediate tables required by the first high-half mapping;
- read/write permission encoding;
- offset-preserving software translation;
- duplicate-map refusal;
- non-canonical-address refusal;
- invalid write-only permission refusal;
- independent hierarchy validation;
- unmapping;
- empty-table reclamation;
- address-space destruction;
- caller-owned data-frame release;
- final physical-memory accounting identical to the baseline.

The readiness marker is descriptive:

```text
UMICOM_KERNEL_VIRTUAL_MEMORY_READY
```
