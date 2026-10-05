# Umicom Kernel — Machine trap entry and return-state hardening

## Why the entry path changes

The original machine entry placed its software frame below the interrupted
stack pointer. That works while the interrupted stack is usable. It cannot
reliably diagnose an instruction that arrived with an unusable stack: even the
first register store would use the wrong address.

The public `UmicomRiscvTrapEntry` symbol now branches to an entry that swaps
onto a dedicated, Kernel-owned stack **before its first memory access**. The
original body remains in `arch/riscv64/trap.S` under `#if 0`, including every
instruction and comment. The explanation immediately above that block names
its replacement and the reason it is no longer executed. The original
`UmicomRiscvTrapDispatch` C implementation is still used for ordinary machine
ECALL and timer handling.

This is the roadmap's trap/privilege hardening work. It is not yet the following
interrupt-ownership, nested critical-section or pre-emptive scheduling work.

## A vector and a scratch pointer belong together

Whenever the ordinary machine vector is installed, `mscratch` contains the top
of its dedicated landing stack. Entry executes:

```asm
csrrw sp, mscratch, sp
```

After that instruction, `sp` names owned machine storage and `mscratch` holds
the interrupted stack pointer. The old pointer is copied into the report. It is
not dereferenced to create the report. The frame remains the established
288-byte integer/CSR frame, aligned to the C calling convention.

The private user and supervisor entry paths already borrow `mscratch` for
their own return contexts. Their restoration now publishes the saved scratch
pointer **before** restoring its matching vector. In the user monitor this is
also done before calling ordinary machine-mode C policy. A Kernel fault during
that C call must not mistake the saved user stack value for a machine landing
address. The existing later restoration instructions remain in place; writing
the same saved CSR again is harmless and keeps the prior source visible.

Only hart zero owns this stack. A future multi-hart implementation needs
per-hart storage and a separately reviewed installation protocol. The stack
margins are canaries, not an unmapped guard region or a memory safety boundary.

## Saving a frame is a critical phase

Once the first scratch register is safely stored, the entry selects a separate
emergency vector. A fault during the remaining save, C policy or restoration
must not restart the first save and overwrite evidence.

The emergency vector does not exchange with `mscratch` and does not trust the
current `sp`. It captures the second fault's CSRs, clears MPRV and interrupt
enables, selects an independent 4 KiB stack, and installs a final stackless
stop vector before calling bounded diagnostics. Ordinary handling has a
separate 16 KiB stack; both belong to the protected Kernel image.

The emergency platform code uses a bounded UART poll rather than the existing
unbounded early-console wait. If the UART cannot accept a byte, that byte can
be lost, but it does not keep the diagnostic loop alive forever. The QEMU test
finisher is then attempted. A further fault uses a stackless fail-stop path.

This is **fail-stop containment**, not recovery of arbitrary broken Kernel
operations. It assumes valid Kernel RAM, supported machine CSRs and the QEMU
platform contract. The short initial landing sequence still requires an intact
installed scratch pointer. NMI handling, hardware double-trap extensions,
SMP, arbitrary nested interrupts and damaged physical memory are not claimed.

## What is checked before returning

`trap_integrity.c` is ordinary freestanding C. It does not access CSRs or follow
instruction pointers. It checks candidate frames against the executable range
published by the linker, then checks the result of the selected policy.

The admission rules require machine origin (`MPP=M`), masked live MIE, and no
unsaved floating-point or vector state. PC arithmetic is checked without
wrapping. A normal ECALL must have cause 11; a machine timer must carry the
interrupt bit and cause 7. Lower-privilege frames use their own private paths.

The runtime wrapper additionally checks the actual frame address, stack
sentinels and the four ECALL instruction bytes, after proving that those bytes
lie inside the executable range. The comparison record is zero-initialised
static Kernel storage: only one non-nesting hart owns it, and every byte is
copied before use. This avoids a hidden hosted `memset` or `memcpy` dependency
while keeping the object defined for compiler diagnostics.

The return checks protect:

* all 31 saved integer registers, including the interrupted stack pointer;
* the captured cause, trap value and reserved word;
* the precise continuation PC;
* every saved status bit except the adjustment explicitly allowed by the route.

The original ECALL route advances PC by four bytes and preserves status. The
original one-shot timer route keeps PC and clears MPIE. No route may set live
MIE before the register restore, select a lower return privilege or accidentally
leave MPRV affecting restoration loads.

MRET itself has architectural effects: it transfers MPIE into MIE, sets MPIE
and resets MPP to the least supported privilege. Trap diagnostic CSRs also
retain the latest event. The test does not confuse those specified effects
with a failure to restore unrelated machine control state.

## Controlled effective-privilege faults

A machine-mode instruction can use MPRV and MPP to request supervisor-effective
data access without changing its instruction-execution privilege. Hardware
records machine origin when that instruction traps; it also overwrites MPP.
A generic handler must not guess the earlier effective-access policy.

Two narrowly armed probes therefore have fixed instruction and cleanup labels:

* a load from an unmapped supervisor address must produce cause 13;
* a store to a read-only supervisor page must produce cause 15 without changing
  the backing data.

Only the exact armed PC, expected cause and machine-origin frame are accepted.
The continuation is fixed by the assembler; no caller supplies an arbitrary
resume address. The wrapper clears MPRV in the saved frame, then returns only
to that instruction's cleanup label. The probe restores its caller's original
status. An unrelated fault remains fatal. A protection operation that does not
fault fails validation rather than being counted as successful containment.

This is a bounded Kernel acceptance mechanism, not a general exception-skipping
API and not a user-visible service.

## Runtime checks

The normal image exercises five cases:

| Case | Required evidence |
|---|---|
| ECALL with `sp=0` | Every integer register survives; the saved frame lies on the owned machine stack. |
| Machine timer with `sp=0` | The actual interrupt returns without using the invalid stack; all non-poll registers survive. |
| Unmapped MPRV load | The precise load instruction faults, then reaches its fixed cleanup label. |
| Read-only MPRV store | The precise store faults and the underlying data remains unchanged. |
| Continuation after faults | Another register probe passes; vector/scratch pair, timer state and frame accounting are restored. |

The register probe uses `sscratch` only to retain its own valid caller stack
while testing `sp=0`. It restores the original `sscratch` and callee-saved
registers before returning. The actual machine entry never relies on that CSR.
The timer probe uses t6 as a polling register; the ECALL probe independently
covers t6 as well as the other integer registers.

## A separate nested-fault test image

The build also produces:

```text
tests/umicom-trap-nested.elf
```

It uses the current Kernel source graph with an explicit, test-only compile
definition. The graph is finalised with CMake DEFER so subsequent additive
modules cannot leave this image with an older source list. It has its own ELF
and map file and cannot replace the normal boot image.

In that image only, a known ECALL enters the machine wrapper and an illegal
instruction is injected while its outer frame is live. The emergency handler
may report test success only when the exact injected PC, cause 2, machine origin
and live outer frame all match. It then terminates QEMU; it does not resume the
broken operation. The normal Kernel contains no nested-fault success branch.

The new CTests require the existing `kernel_current_image` fixture. Its normal
build target also builds the fault image, preventing an obsolete fault-test ELF
from being used after compilation fails. The first build has extra compilation
because there are two independently compiled Kernel images, not because the
normal incremental build directory has been discarded.

## Normal Windows workflow

No new libraries, scripts or tools are required.

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Keep the existing QEMU command for the normal image. CTest separately launches
the nested-fault image. The new tests are `kernel.riscv64.trap_integrity` and
`kernel.riscv64.trap_nested_fault`, bringing the main suite to fifteen tests.
Do not remove the build directory or change line-ending policy for this update.

## Optional native policy tests

On a native host with CMake, Ninja and a C compiler:

```text
cmake -S tests/trap_integrity -B build/native-trap-policy -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-trap-policy --parallel 2
ctest --test-dir build/native-trap-policy --output-on-failure --no-tests=error
```

`UMICOM_TRAP_SANITIZERS=ON` additionally enables address and undefined-behaviour
sanitizers when the selected host toolchain supports them. These tests compile
the real C validator and the original C cause dispatcher; platform operations
are counted host substitutes. They do not execute the new RV64 entry.

## Architecture references

The implementation follows the RISC-V machine-level specification's sections
on the privilege/interrupt stack, MPRV, mscratch, trap state and MRET:
https://docs.riscv.org/reference/isa/priv/machine.html

The specification explicitly warns that a fault during critical trap-state
preservation can overwrite information needed to recover. This code does not
assume that disabling ordinary interrupts also prevents synchronous exceptions.

CMake's deferred configuration and fixture behaviour are documented at:
https://cmake.org/cmake/help/latest/command/cmake_language.html
https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html

The established CTests use readiness regular expressions. CMake documents that
PASS_REGULAR_EXPRESSION can override a nonzero process exit; manual validation
must still check the QEMU exit status. This update does not silently alter the
existing test properties:
https://cmake.org/cmake/help/latest/prop_test/PASS_REGULAR_EXPRESSION.html

## Next planned work

The next work package is explicit interrupt ownership and nested critical
sections. It must define masking tokens, ordering and safe shared-state access
before timer-driven user-context switching is introduced. Neither the existing
cooperative scheduler nor its event API becomes IRQ-safe merely because trap
landing is now protected.
