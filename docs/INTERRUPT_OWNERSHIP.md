# Umicom Kernel — Interrupt ownership and nested critical sections

## The problem this solves

A statement such as “disable interrupts, do some work, enable interrupts” looks
reasonable until one helper calls another helper which follows the same rule.
The inner helper enables interrupts while its caller is still updating shared
state. It has accidentally ended a critical section that it did not own.

The new controller records the outer state and the nesting order explicitly.
Entering atomically captures and clears the machine interrupt-enable bit.
Leaving an inner section keeps it clear. Only the matching outer leave restores
the state that existed before entry. A caller that already had delivery disabled
therefore keeps it disabled.

A second problem is device ownership. Two pieces of Kernel code must not both
assume they may reprogram the same timer. A source lease records the owner and
prevents ordinary thread switching or loaded-process entry until it is released.
That prevents the existing user monitor from borrowing an already-owned timer,
even when its source is temporarily disabled.

These are foundations for safe scheduling boundaries, not pre-emption itself.

## What stays unchanged

The ordinary timer interrupt still goes through the previously hardened machine
entry, its return-state checks, and the original C cause dispatcher. Its existing
one-shot acknowledgement policy remains in service. There is no new timer ISR.

The platform timer, trap implementation, scheduler C implementation, events,
messages, memory management and executable loader are not rewritten. Two narrow
admission checks are added to the RV64 cooperative-context adapter and the
loaded-process execution adapter. Existing code and comments remain verbatim.
No old implementation needs disabling in this change.

## Architecture and terminology

MIE is the global machine-mode interrupt-enable bit in `mstatus`. Individual
source bits live in `mie`, and pending observations live in `mip`. They are not
the same kind of state.

Clearing MIE prevents eligible machine interrupts while executing in machine
mode. It does not clear an interrupt source's pending condition. A timer may
become pending while delivery is masked, then enter the existing handler as soon
as the outer section restores MIE.

The save primitive uses one `csrrci` instruction to capture and clear MIE. A
separate read followed by a clear would leave an interrupt window between them.
The restore primitive changes MIE only. Restoring an earlier *whole* `mstatus`
word could overwrite unrelated privilege or trap-return fields.

The C barriers and `fence iorw, iorw` instructions order local memory/device work
around the ownership boundary. They are not an SMP lock, do not stop DMA, and
do not mask synchronous exceptions or non-maskable events.

Architectural references:

- [RISC-V machine interrupt state](https://docs.riscv.org/reference/isa/v20260120/priv/machine.html)
- [CTest fixture prerequisites](https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html)

## One controller, one hart, one lifetime

The controller uses private static storage for hart zero. There is deliberately
not a caller-created collection of independent domains: two domains controlling
the same MIE bit could each restore it without knowing about the other.

Initialise once with the installed ordinary machine vector and its matching
landing-stack address. Admission requires MIE clear, no enabled source, Bare
translation, and no effective-privilege, floating-point or vector state to
inherit. Calls require a valid machine-mode stack and trusted Kernel pointers.

The vector/scratch pair is rechecked. During ordinary trap handling the emergency
vector is installed, and private user/supervisor paths use a different entry
pair. Those contexts cannot quietly join a thread-owned critical section just
because their live MIE happens to be clear. This is not a universal detection
mechanism for arbitrary external interrupt handlers: the current entry protocol
is a prerequisite.

In the cumulative acceptance image, initialisation happens in the new validation
call after earlier capabilities complete. A persistent OS boot sequence must
initialise the controller after installing its machine vector and before starting
services which claim interrupts. Early code before initialisation retains its
previous architecture admission rules.

## Section owners and tokens

An owner is a nonzero execution-context identity selected by trusted Kernel code.
It is not a user argument, a process capability, a random secret or automatically
derived from the cooperative scheduler. Nested helpers in the same invocation
must pass the same identity.

Each successful Enter issues a fresh token and retains the old MIE bit. Tokens
must be closed in reverse order. Wrong-owner, stale-token, duplicate-leave and
out-of-order attempts do not pop a record or re-enable delivery. The limit is
sixteen nested records. A failed Enter does not change its output token.

The guard protects a short synchronous operation, not the entire lifetime of an
application or a service. Check every return status:

```c
UmicomKernelCriticalSection section = 0U;
UmicomKernelInterruptStatus result =
    UmicomKernelCriticalSectionEnter(contextIdentity, &section);
if (result != UMICOM_INTERRUPT_OK) {
    /* No ownership was acquired. Do not perform the protected mutation. */
    return result;
}

/* Update the small piece of Kernel state owned by this synchronous operation.
 * Do not yield, enter a user program or wait for another callback here. */

return UmicomKernelCriticalSectionLeave(contextIdentity, section);
```

Tokens share a monotonic counter with source leases, so a live token cannot be
accidentally reused for the other kind of object. The final value is issued only
once; there is no wrap. Exhaustion refuses new acquisitions instead of reviving
old tokens. Existing open records remain closable. There is no reset/recovery
API which discards outstanding ownership; this intentionally conservative early
policy can require stopping service at exhaustion rather than promising unlimited
uptime. It is not a configurable general-purpose lock manager.

## Source leases

Only the machine-timer source is supported. Claiming an external-controller or
unknown mask is refused; there is no PLIC implementation hidden behind the API.

Acquire, Enable, Disable and Release require an open section with the same owner.
A lease remains live after that section closes, until explicitly released.
Holding a lease does not itself enable either the source or global delivery.

Acquire requires that the source is disabled, not delegated, not pending and
quiesced by the platform. Release has the same disabled/not-pending/quiescent
requirements. The QEMU check reuses `UmicomPlatformTimerCompareRead()` and requires
the maximum compare value used by the existing timer-disable operation.

Why inspect the device as well as `mie` and `mip`? A timer whose future deadline
has not yet elapsed can have both bits clear. Releasing it then would let a new
owner inherit that earlier deadline. The owner must park the device explicitly;
the ownership service will not silently overwrite a deadline to make a release
look successful. The maximum compare value is a practical early-boot parking
convention, not a proof of behaviour at timer wraparound.

Named source bits are changed with CSR set/clear operations, leaving unrelated
bits alone. Readback verifies that the hardware accepted the change. An unexpected
readback leaves delivery masked and marks the controller poisoned.

`UmicomKernelInterruptDeliverySet()` is an explicit outer policy operation. It
cannot run with any section open. Enabling requires that the caller owns every
currently enabled source, at least one source is enabled, and the owned source
has not been delegated elsewhere. It changes MIE only.

The original timer handler can subsequently clear its source and global delivery
according to its existing one-shot policy. A snapshot showing MIE clear after the
pending-timer test is therefore correct: the outer leave restored MIE, the
interrupt ran, and the handler disabled it again.

## Refusal to abandon ownership

The existing `UmicomKernelThreadMachineReady()` first asks the new ownership
predicate when this module is linked. Yield, Wait, a real Sleep and RunOne then
refuse an unfinished section or timer lease before changing a thread's state.
A sleep whose deadline has already passed remains an immediate return, not a
context switch. The scheduler's C policy is unchanged.

The low-level cooperative context switch repeats the check. Callback completion
reaches that primitive directly; a callback that returns with ownership still
held must fail-stop before changing stacks. It cannot safely pretend it released
someone else's section. This is a programming error, not a recoverable result
from that non-returning completion path.

The loaded-process adapter checks before clearing the terminal report, changing
process state or programming its deadline. The caller may release ownership and
retry the still-READY image.

These checks cover the normal thread and process paths, not every privileged
instruction that a trusted callback could execute. Direct raw CSR writes and
private user/supervisor entry primitives remain internal APIs with their existing
caller contracts. The Kernel is not sandboxed against its own machine-mode code.

## Unexpected state changes fail closed

While a section is open, the controller checks that MIE was still clear before
its next operation and that `mie`, delegation and the vector/scratch binding
remain consistent with its recorded operations. A detected violation poisons the
controller and keeps MIE masked. It does not restore a stale entire CSR word or
pretend the original protected operation completed safely.

SnapshotRead can report the poisoned state without clearing it; it returns
POISONED with a diagnostic snapshot. Other unsuccessful operations leave normal
output tokens untouched. Snapshots are observations at call entry, not promises
about device state after interrupts are restored.

A raw MIE write which is enabled and disabled again between checks may escape
these checks. Preventing arbitrary machine-mode code from doing that is not the
claim. The design requires cooperative trusted callers and explicit ownership.

## Guest acceptance sequence

The added test follows the established cumulative boot checks:

1. Fill all sixteen nesting slots; reject another owner, excess depth and an
   outer-before-inner leave; retain the originally disabled state and reject a
   stale token in a new section.
2. Exercise exclusive timer leases, unsupported sources, wrong owners, release
   while enabled, explicit global enabling inside a section, and a future timer
   deadline that has not been quiesced.
3. Enable an owned parked timer, enter two sections, program a short deadline
   and observe the real pending bit. Reject the out-of-order leave and show the
   inner leave still cannot deliver the interrupt. The outer leave must allow
   exactly one invocation of the unchanged timer policy.
4. Run a real cooperative callback. Yield, Wait and sleeping into the future
   refuse inside its section. After release, Yield succeeds and its local value
   survives resumption. A dispatcher with an open section also cannot RunOne.
5. Load the existing diagnostic ELF. An open section and then a disabled but
   owned timer both refuse entry without changing READY or stealing the deadline.
   After release the program runs, exits with the expected value and is destroyed.

The final checks exercise the original machine ECALL again, compare control
state and timer compare, and verify physical-frame accounting. MPP/MPIE and trap
diagnostic CSRs naturally reflect the architectural trap history; unrelated
control state is compared separately.

Success ends with `UMICOM_KERNEL_INTERRUPT_OWNERSHIP_READY`. The new controller
and validation scheduler use static storage, so the protected Kernel image grows.
Its absolute end address and reserved-frame count are not fixed test constants.

## Normal Windows workflow

Keep the existing incremental build directory. No new tool or library is needed.

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run the same QEMU command as before. There is one new runtime test:
`kernel.riscv64.interrupt_ownership`. It requires `kernel_current_image`, including
the existing nested-fault test image dependency. The normal suite has sixteen
tests when QEMU is available.

## Optional native checks

The policy suite builds the actual C controller and board quiescence adapter,
with an explicitly modelled CSR boundary. On a Linux x86-64 development host:

```text
cmake -S tests/interrupts -B build/interrupts-native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug
cmake --build build/interrupts-native --parallel 2
ctest --test-dir build/interrupts-native --output-on-failure --no-tests=error
```

The sixty-four policy cases use ASan/UBSan when enabled. Four extra tests on
Linux x86-64 reuse the existing real alternate host stacks, with linker wrappers
modelling the new RV64 readiness branches. Those four use UBSan only because the
host context adapter does not implement ASan fibre notifications. Neither suite
is RISC-V execution evidence.

## Next planned dependency

Timer-driven runnable user contexts come after this ownership boundary has passed
on the guest. That work must save the full interrupted context, distinguish a
pre-emption tick from a terminal deadline, coordinate runnable ownership and
resume at the interrupted PC. This controller does not turn the current
cooperative scheduler or events into pre-emptive or SMP-safe services.
