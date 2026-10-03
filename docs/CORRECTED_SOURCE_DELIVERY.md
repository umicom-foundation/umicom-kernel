# Corrected source delivery

This delivery replaces the earlier virtual-memory overlay that accidentally
allowed short `Umi...` names to remain active through compatibility aliases.

## What is corrected

The active Umicom Kernel source now uses only the full project names:

```text
UmicomKernel...
UmicomPlatform...
UmicomRiscv...
UMICOM_KERNEL_...
UMICOM_RISCV_...
```

Earlier abbreviated names remain visible only in disabled historical blocks.
They are not compiled and cannot redirect a full `Umicom...` name back to an
older short symbol.

## Preservation rule followed by this delivery

No existing implementation logic was deliberately deleted.

No educational comment was deliberately shortened merely for style.

Where an old naming mechanism is superseded, the old declarations or aliases
remain inside a documented `#if 0` block so readers can compare the earlier and
current forms.

Historical documentation already committed to the repository is left in place.
Git history remains the record of chronology.

## New functionality

The delivery also adds the Sv39 virtual-memory foundation:

- three-level page-table construction;
- 4 KiB mappings;
- canonical-address checks;
- RISC-V permission validation;
- software translation;
- duplicate-map refusal;
- unmapping;
- empty intermediate-table reclamation;
- physical-frame accounting and leak checks.

The CPU is not switched into translated execution yet.  `satp` activation is a
separate capability so the Kernel can first prove that page-table construction
is correct and recoverable.
