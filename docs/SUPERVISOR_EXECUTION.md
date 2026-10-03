# Umicom Kernel — controlled supervisor execution

## What this adds

The existing hardware-translation check remains unchanged. It uses MPRV to
translate a data access while instructions still execute in machine mode.
This new check goes further: `mret` enters supervisor mode, where the processor
fetches a small C payload through an Sv39 page table and uses a separately
mapped stack.

This is deliberately a round trip, not a permanent move of the whole Kernel
into supervisor mode. Machine-mode startup prepares the experiment, supervisor
code performs one bounded operation, and a private machine trap vector returns
the result to the original caller. All earlier Kernel checks run first.

```text
Existing machine-mode Kernel checks
    |
    +-- prepare page tables and caller-owned data frames
    +-- save machine control state and machine stack
    +-- install a private temporary trap vector
    |
    mret: actual privilege changes to supervisor
    |
    +-- translated instruction fetch
    +-- ordinary C function using a translated stack
    +-- shared observation-page writes
    +-- completion ECALL or deliberate protection fault
    |
    machine trap: swap onto saved machine stack immediately
    |
    +-- record cause, PC, fault value and previous privilege
    +-- restore the original control state
    +-- return to the existing machine-mode C caller
    +-- validate observations and release temporary frames
```

## Why the established trap handler is untouched

The normal trap handler already supports the Kernel's machine-mode exception
and timer checks. A supervisor fault introduces a different problem: its stack
pointer is a supervisor virtual address and may not be suitable for a machine
handler's first memory access.

Rather than changing that established handler in the same update, this
experiment uses `UmicomRiscvSupervisorTrapEntry`. Before entry, `mscratch`
contains the saved machine context pointer. The private vector begins with:

```asm
csrrw sp, mscratch, sp
```

The swap supplies a known machine stack before the handler performs any store.
It also preserves the interrupted supervisor stack pointer for the report.
The handler clears live MPRV before accessing machine memory, captures the
supervisor event, and returns to the saved machine caller. It does not skip a
faulting instruction or pretend that a rejected operation completed.

The original `mtvec` is restored on return. After all supervisor cases, a new
call to the existing machine-mode ECALL helper checks that the original trap
dispatcher still receives the event and increments its counter correctly.

## The small supervisor memory map

| Region | Mapping | Permissions and ownership |
|---|---|---|
| Dedicated payload text | Identity mapped at its linked address | Read/execute; never writable through this page table |
| Supervisor stack | `0x0000001000010000`, one 4 KiB page | Read/write, non-executable; separately allocated physical frame |
| Shared observation | `0x0000001000020000`, one 4 KiB page | Read/write, non-executable; machine code reads the same frame physically |
| Read-only sentinel | `0x0000001000030000`, one 4 KiB page | Read-only, non-executable; backing word checked after the attempted write |
| Guard below stack | One page immediately below the stack | Intentionally unmapped |

The linker brackets `.text.umicom_supervisor_payload` with page-aligned symbols.
Only this range is mapped executable. The ordinary Kernel code, machine stack,
report, UART and timer MMIO are absent from the supervisor mapping.

The payload cannot call the normal console or allocator through this prepared
mapping. It uses a shared observation page instead. Machine mode prints the
results after returning. This keeps the test independent of a supervisor console
service or a new system-call interface.

The payload's compiler options disable jump tables. Its linked references were
inspected in both Debug and Release builds to check that its calls remain within
the mapped payload section and that no hosted runtime call is introduced.

## Page-table and data-frame ownership

The existing physical allocator remains the ownership authority. The existing
virtual-memory subsystem owns its root and intermediate page-table frames.
The validation owns its three data frames: stack, observation and sentinel.

Allocated data pages are explicitly cleared before being exposed to the payload.
The original allocator does not promise to erase a frame merely by allocating it.

On completion, the saved `satp` is restored and translation state is fenced
before the new page tables are destroyed. Address-space destruction frees only
page-table frames. The caller then frees its three data frames separately.
An independent allocator recount and a comparison with the starting counters
must pass before the supervisor readiness marker can be printed.

## Physical Memory Protection during the experiment

The temporary PMP entry covers the allocator's naturally aligned RAM range,
using a NAPOT encoding derived from that range. It does not grant UART or other
MMIO access. The entry is unlocked and the previous PMP address/configuration
is restored after each call.

This is **not a production security policy or a sandbox for hostile code**.
The experiment runs a fixed, trusted payload on one active hart. Its temporary
RAM-wide PMP permission is broader than a future isolated service should have,
and supervisor code is not prevented from changing `satp` by a complete monitor
policy. The checks establish architectural privilege and permission behaviour,
not an untrusted-process boundary.

The transition refuses a nonzero first PMP configuration bank rather than
silently overwriting an existing policy. It also requires machine-mode callers
to use ordinary physical accesses with interrupt delivery disabled. It is not a
general context-switch API, and it does not manage floating-point, vector, SMP,
non-maskable-interrupt or arbitrary third-party supervisor state.

## Five runtime cases

| Case | Expected hardware trap | Additional check |
|---|---:|---|
| Normal C return | Supervisor ECALL, cause 9 | C result, completion cookie, exact ECALL PC |
| Load from unmapped guard | Load page fault, cause 13 | Exact load PC and faulting virtual address |
| Write through read-only mapping | Store page fault, cause 15 | Exact store PC, address and unchanged physical sentinel |
| Read machine-only `mstatus` | Illegal instruction, cause 2 | Exact CSR instruction PC |
| Normal return after faults | Supervisor ECALL, cause 9 | Recovery permits another successful round trip |

For every case, the trap-entry MPP field must identify supervisor mode. The
payload must have read the expected `satp`, used an in-range stack, and computed
`0x404` through volatile stack locals. Stack-bottom canaries must survive.
The saved machine CSR snapshot must be restored exactly.

The guard case deliberately loads from the unmapped page. It is not a claim
that arbitrary stack overflow or a damaged stack has been exhaustively tested.
Illegal-instruction `mtval` is not forced to contain a particular value because
permitted implementations may report zero or instruction information.

Expected page faults are positive test cases only when all those checks agree.
An unrelated exception, a wrong fault address, an unexpected completed flag or a
changed sentinel is a failure, not a successful protection test.

## Runtime evidence

Each case prints measured observations after the machine caller regains control:

```text
supervisor.case=normal-return
supervisor.previous-privilege=1
supervisor.trap.cause=9
supervisor.trap.pc=0x...
supervisor.trap.value=0x...
supervisor.observed-satp=0x8...
supervisor.observed-stack=0x...
supervisor.stack-result=0x0000000000000404
supervisor.machine-state=restored
supervisor.case-result=pass
```

The five cases must end with:

```text
supervisor.original-trap-handler=pass
supervisor.frame-accounting=restored
supervisor.completed-cases=5
supervisor-execution-test=pass
UMICOM_KERNEL_SUPERVISOR_EXECUTION_READY
UMICOM_KERNEL_END
```

Addresses vary as the linked image grows. Cause numbers, case identities,
observed privileges and accounting requirements do not.

## How to read the implementation

Start with `include/umicom/kernel/riscv64/supervisor.h` for the request, report,
observation and C/Assembly offset assertions. Then read
`kernel/supervisor_payload.c`, which is the code that actually runs at supervisor
privilege. `arch/riscv64/supervisor.S` explains the entry, private trap and return
path. Finally, `kernel/supervisor_validation.c` builds the mappings and checks the
results from machine mode.

The existing trap implementation, platform interfaces, UART/timer code,
physical allocator and Sv39 mapper are not rewritten or replaced.

## Architectural references

These public specifications describe the hardware mechanisms used here; the
Umicom implementation is original project code.

- RISC-V Machine-Level ISA: MPP/MRET, MPRV, trap causes, `mscratch` and PMP:
  https://docs.riscv.org/reference/isa/v20260120/priv/machine.html
- RISC-V Supervisor-Level ISA: `satp`, Sv39 permissions and `sfence.vma`:
  https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html

## What remains separate work

The rest of the Kernel still runs in machine mode. There is no general
supervisor service manager, user-mode process, scheduler, interrupt-driven
supervisor timer, production PMP policy or multi-hart execution in this change.
Those capabilities need their own designs and runtime evidence. This change
provides a bounded, inspectable privilege transition on which to build them.
