# Umicom Kernel Virtual Memory Delivery Contents

## Purpose

This replacement delivery corrects the earlier overly broad source-cleanup
attempt and adds the Sv39 virtual-memory foundation without discarding existing
Kernel logic or educational commentary.

## Preservation guarantees

- Existing implementation logic is preserved unless the virtual-memory feature
  requires an additive integration point.
- Existing educational comments are retained. Identifier-only changes change
  identifiers, not the surrounding explanation.
- Canonical Kernel identifiers use the full `Umicom...` spelling.
- Legacy short `Umi...` names, where retained for source compatibility, point
  to the full Umicom names and are isolated in clearly labelled compatibility
  sections.
- No Python, PowerShell or shell script is added.
- No existing source file is physically deleted by this delivery.

## New implementation files

- `include/umicom/kernel/virtual_memory.h`
- `kernel/virtual_memory.c`
- `docs/VIRTUAL_MEMORY.md`
- `docs/SOURCE_NAMING_AND_COMMENTING.md`

## Existing source files modified

- `CMakeLists.txt`
- RISC-V bootstrap/trap source
- Kernel console, entry, physical-memory and platform contracts/implementation
- recurring build/test and architecture roadmap documentation

The modifications establish full Umicom naming, descriptive capability test
markers and virtual-memory integration while preserving existing logic.

## Documentation renames

The following historical guides are renamed descriptively with `git mv` so Git
history is retained:

- `K1_FIRST_BOOT.md` -> `FIRST_BOOT.md`
- `K2_TRAPS_TIMER.md` -> `TRAPS_AND_TIMER.md`
- `K2_WINDOWS_BUILD_AND_TEST.md` -> `TRAPS_TIMER_WINDOWS_BUILD_AND_TEST.md`
- `K2_DELIVERY_MANIFEST.md` -> `TRAPS_TIMER_DELIVERY_MANIFEST.md`
- `K3_PHYSICAL_MEMORY.md` -> `PHYSICAL_MEMORY.md`
- `K3_DELIVERY_MANIFEST.md` -> `PHYSICAL_MEMORY_DELIVERY_MANIFEST.md`

These are renames and terminology corrections, not history deletion.

## Deleted files

None.

## Virtual-memory scope

The added Sv39 subsystem constructs, validates, translates and tears down
three-level page tables using physical frames from the existing physical-memory
allocator. CPU address translation remains disabled; `satp` activation is a
separate future capability so the Kernel can validate page-table ownership and
failure paths before depending on them for execution.
