# Umicom Kernel — Trap hardening validation evidence

## Exact starting point

Repository: `umicom-foundation/umicom-kernel`

Baseline commit: `2a981dbb6b99f68a71030ec0377181cfb31efaaa`

The six modified source/build files were compared with the Git blob contents
from that commit. The overlay does not attempt to roll back any of the owner's
previously merged comments or implementation choices.

## Preservation is different from leaving behaviour unchanged

Every original line in each modified file is still present, unchanged and in
its original order. The changes are:

| File | Insertions | Removed/reworded lines |
|---|---:|---:|
| `CMakeLists.txt` | 4 | 0 |
| `arch/riscv64/linker.ld` | 5 | 0 |
| `arch/riscv64/supervisor.S` | 9 | 0 |
| `arch/riscv64/trap.S` | 18 | 0 |
| `arch/riscv64/user_execution.S` | 14 | 0 |
| `kernel/main.c` | 7 | 0 |

There is one deliberate execution change: the ordinary machine trap entry now
jumps to the dedicated-stack implementation. The old 170-line entry body,
including comments and blank lines, remains inside an explained `#if 0` block.
It is preserved for comparison, not silently left executing as a second entry.
The original C cause dispatcher still handles ordinary ECALL and timer policy.

The user and supervisor Assembly gain ordered vector/scratch restoration and a
private-frame emergency-vector hand-off. No original instruction is removed.
All original platform implementations, public platform header, allocator,
page walker, monitors in C, process services, scheduler and events are unchanged.
There are no newly introduced abbreviated project aliases or script files.

## Available tools and what was not available

Cross-build host: Linux x86-64.

* Clang and LLD 17.0.0.
* CMake 3.31.6 and Ninja.
* GCC for an independent native C policy build.
* GNU ELF readers for linked-header and symbol inspection.

The user's Windows Clang 22.1.8/MSYS2 environment was not available. Neither was
`qemu-system-riscv64`. This delivery therefore makes **no claim of a local QEMU
boot, real interrupted-register restoration, page-fault recovery, nested trap
execution or Windows-compiler qualification**. Those are the supplied guest
acceptance steps, not results inferred from successful compilation.

## Completed build checks

* The complete existing baseline was cross-built before applying the change.
* Complete Debug and Release builds produced the normal Kernel, its separate
  nested-fault test image, and both existing independent user programs.
* A repeated incremental build reported `ninja: no work to do`.
* Source and build paths containing spaces configured and built successfully.
* The finished ZIP was applied to a separate baseline copy and the complete
  source was cross-built again. An immediate second build was incremental.
* ELF readers reported a little-endian RV64 executable whose entry remains
  `0x80200000`. The public trap-entry symbol is a small branch, not a duplicate
  copy of the disabled old entry.
* The fixed load/store recovery labels are exactly four bytes after their
  faulting instructions. The linked normal trap stack and emergency stack lie
  within the reserved Kernel BSS range.

The extra fault image is separately compiled with an explicit test definition.
Its extra object builds are expected; the update does not discard the developer's
incremental build tree.

## Native policy tests actually executed

The new native suite compiles `kernel/trap_integrity.c` and the **unchanged**
`arch/riscv64/trap.c`. Machine timer reads/writes and hart identity are counted
host substitutes. The suite does not execute trap Assembly or inspect real CSRs.

| Configuration | Result |
|---|---:|
| Clang Debug, AddressSanitizer + UndefinedBehaviorSanitizer | 70/70 passed |
| Clang Release, AddressSanitizer + UndefinedBehaviorSanitizer | 70/70 passed |
| GCC Debug, AddressSanitizer + UndefinedBehaviorSanitizer | 70/70 passed |

Coverage includes all 31 saved integer registers independently, including sp;
wrong privilege; live MIE; unsaved floating/vector state; invalid causes;
out-of-range, odd and overflowing PCs; wrong continuation length; changes to
cause/value/reserved evidence; and forbidden return-status changes. Separate
cases call the actual original ECALL and one-shot timer policy functions and
validate their returned frames.

A frame-policy test proves that an input/output pair is accepted or refused. It
does not prove that hardware filled that frame correctly or that the assembly
restored it; the zero-stack guest probes provide that separate evidence.

## Existing native regressions actually executed

| Suite | Result | Instrumentation |
|---|---:|---|
| Remembered events | 48/48 | UndefinedBehaviorSanitizer |
| Cooperative threads | 55/55 | UndefinedBehaviorSanitizer |
| Message channels | 72/72 | AddressSanitizer + UndefinedBehaviorSanitizer |
| Process registry, including a real linked ELF input | 49/49 | AddressSanitizer + UndefinedBehaviorSanitizer |
| Executable loading, including a real linked ELF input | 68/68 | AddressSanitizer + UndefinedBehaviorSanitizer |
| User-memory boundary | 24/24 | AddressSanitizer + UndefinedBehaviorSanitizer |

The events/threads tests use the established Linux alternate-stack adapter;
no ASan fibre-switch claim is made for that adapter. Loading an RV64 ELF in a
native test validates bytes and ownership, not execution of its instructions.

## Test registration and stale-image rejection

An isolated copy was configured with `/bin/false` as a deliberately non-emulating
command **only to inspect test registration and fixture ordering**. It was not
used to claim any runtime result. The test catalogue contained fifteen tests;
both new tests required `kernel_current_image` and selected their distinct ELFs.

A compile-time error was then deliberately added in that private copy. Running
only the two new capability tests automatically included `kernel.build.current`.
That fixture failed, and both dependent tests were reported `Not Run`. The error
was removed from the private copy and the current source built successfully.
No deliberate error, stand-in executable path or qualification script is in
the delivery. The normal QEMU discovery code is unchanged.

## Guest acceptance still required

Normal image:

1. All previous capability checks must still pass under the new machine entry.
2. ECALL with a zero interrupted stack must preserve all integer registers.
3. A real machine timer with a zero interrupted stack must preserve the
   non-poll registers and use the owned machine frame.
4. The exact armed MPRV load/store instructions must fault and reach their
   fixed cleanup labels; the read-only data must remain unchanged.
5. A later ECALL must still pass, and vector/scratch, timer and frame accounting
   must be restored.

The normal completion marker is `UMICOM_KERNEL_TRAP_INTEGRITY_READY`, followed
by the established `UMICOM_KERNEL_END`. Check QEMU's exit code is zero.

Separate fault image:

A real illegal instruction must be injected inside an active machine handler.
Only its exact PC, machine-origin cause and live outer frame can produce
`UMICOM_KERNEL_TRAP_NESTED_REJECTION_READY`. The guest then exits; it does not
attempt to continue from the broken handler. The normal image cannot report a
nested machine fault as a success.

The source follows the current capability-test style. CMake's
`PASS_REGULAR_EXPRESSION` may override a nonzero normal process exit, so a regex
alone is not a universal exit-status verifier. The guest marker checks are
strict, timeouts remain failures, and manual qualification must still inspect
`$LASTEXITCODE`. Existing test properties are not rewritten by this update.

## Limits that remain

This is a single-hart, non-nesting machine trap entry with a fail-stop path for
unexpected reentry. It assumes valid owned RAM and a correctly installed
vector/scratch pair. It does not promise recovery from corrupted physical RAM,
NMI, hardware double-trap extensions, arbitrary nested interrupts, multi-hart
races, stack overflow beyond diagnosable margins, or broken MMIO hardware.
It does not make cooperative callbacks pre-emptive or event APIs IRQ-safe.

The next roadmap package remains interrupt ownership and nested critical
sections, followed by resumable user scheduling. Those responsibilities are
not silently folded into this entry repair.
