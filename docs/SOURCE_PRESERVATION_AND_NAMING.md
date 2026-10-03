# Umicom Kernel source preservation, naming and educational commenting

Umicom Kernel is both production-oriented systems code and a learning resource.
A future developer should be able to understand not only what a line does, but
why the architecture needs it and how the design evolved.

## Preserve existing source by default

Existing implementation logic, comments, headers, authorship, licences and
educational explanations are project assets.

Do not remove them merely because:

- a name changes;
- a subsystem is refactored;
- a newer implementation is preferred;
- a comment looks verbose;
- a shorter explanation could be written.

When an old implementation must no longer compile or execute, keep it in a
clearly labelled disabled block when practical:

```c
/*-----------------------------------------------------------------------------
 * SUPERSEDED IMPLEMENTATION — RETAINED FOR ENGINEERING REVIEW
 *
 * Explain why this code is no longer active, what replaces it, and why the
 * historical implementation remains useful to readers.
 *---------------------------------------------------------------------------*/
#if 0

/* Original implementation remains here. */

#endif
```

Physical deletion requires an explicit decision from the project owner rather
than being treated as ordinary cleanup.

## Do not manufacture historical compatibility code

Source preservation means preserving code that really belonged to the project.

It does not mean inventing compatibility aliases after a rename and then
retaining those invented aliases as though they were valuable historical
implementation.

The previously introduced abbreviated-name compatibility aliases were an
engineering mistake and have been removed by explicit owner direction.  Git
history already records that mistake; the active source does not need to keep
repeating it.

## Full Umicom names are canonical

Active Umicom Kernel code uses the full project name:

```text
UmicomKernel...
UmicomPlatform...
UmicomRiscv...
UMICOM_KERNEL_...
UMICOM_RISCV_...
```

Earlier abbreviated `Umi...` spellings are not active aliases.  Where those
names are useful for understanding earlier commits, retain them in disabled
historical blocks with an explanation.

Do not write an active alias that maps a full Umicom name back to an abbreviated
name.

## Comments should sound like an experienced engineer teaching another human

A useful comment answers one or more of these questions:

- What architectural rule is being implemented?
- Why is this ordering important?
- Who owns this memory or object?
- What failure is being prevented?
- Why is this hardware register touched?
- What assumption does this code rely on?
- What would break if this statement were removed?
- What evidence proves the operation succeeded?

Avoid comments that merely translate syntax into English.

For example, prefer:

```c
/*
 * Program the compare register before enabling timer interrupts.  If the
 * interrupt source were enabled first, an old compare value could already be
 * due and the hart could enter the handler before the new deadline exists.
 */
UmicomPlatformTimerSetCompare(hartId, deadline);
```

over:

```c
/* Set timer. */
UmicomPlatformTimerSetCompare(hartId, deadline);
```

## Chronology belongs in Git history

Working source describes behaviour and architecture.  Git commits record when
a capability was introduced.

Do not embed temporary batch numbers, milestone numbers or development version
labels into active function names, constants, test names or explanatory
comments.

Historical documents already committed to the repository remain part of the
project record unless the project owner explicitly approves their removal.
