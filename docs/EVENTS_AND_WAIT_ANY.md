# Umicom Kernel — Remembered events and wait-any

## Why an event is not just a thread wake

The existing scheduler deliberately stores no future credit when a caller
wakes an already-ready thread. That is a useful low-level rule, but services
also need a state they can set before a consumer starts waiting.

An event remembers that state. A manual-reset event remains signalled until
reset. An auto-reset event awards one waiting callback or retains one credit.
Several signals without an intervening claim coalesce into that one credit.
It is not a counting semaphore and must not be used to count messages.

The new layer calls the existing scheduler APIs. It never writes scheduler
records and does not change the context switch, trap vector or timer policy.

## Objects and lifetime

Create one stable, zero-filled `UmicomKernelEventDomain` after initialising its
scheduler. The scheduler outlives the event domain. Do not copy or reinitialise
either live object.

There are sixteen event slots, eight wait records and up to eight events per
wait. Tokens use an index and generation; generation exhaustion retires a slot.
Tokens belong to their trusted domain, not a global namespace. This Kernel-only
API has no user ownership or rights surface. It is not an extension of the
existing user message ABI.

Close completes pending waits with `CLOSED` before invalidating the event.
Another use of the old token is `INVALID_HANDLE`. Completed results can report
the old token even if its slot has since been reused; that result does not make
the token valid again.

No heap or physical frame is allocated by this service. Domain storage and the
validation scheduler's static stacks increase the linked Kernel's protected
BSS reservation.

## Waiting without losing a signal

A callback passes its current scheduler-issued thread handle to
`UmicomKernelEventWaitAny()`. That handle must identify the running callback,
not another ready or suspended thread.

The service preflights the entire event array before consuming a credit. A
bad second handle therefore cannot consume the first event's signal. It copies
the handles into domain-owned storage before suspension.

Within the current serialised, interrupts-disabled cooperative contract there
is no yield between checking the event state, registering the waiter and
suspending. Another callback cannot run in that interval. A signal can therefore
either be observed immediately or complete the published wait.

The completion is written before the thread is woken. Reset after that award
does not retract it. Raw `UmicomKernelThreadWake()` remains only a hint; a waiter
woken that way rechecks its result and suspends again if nothing completed it.

This argument is not valid for arbitrary interrupt handlers or multiple harts.
The API is deliberately not IRQ-safe, pre-emptive or SMP-safe. Those contexts
require a separate critical-section and memory-ordering design.

## Auto reset, broadcast and fairness

Manual-reset Signal completes every eligible existing waiter and leaves the
event signalled. Subsequent waiters can complete too, until Reset.

Auto-reset Signal completes the oldest eligible registration, then clears the
credit. If nobody waits, the credit remains for a later caller. FIFO concerns
registration order, not creation order of the threads. Within one wait-any set,
array order breaks a tie between already-signalled events.

A completed callback is not guaranteed to be the next thread dispatched. The
existing scheduler still controls runnable-thread selection. This layer promises
an award order, not a scheduling priority.

## Timeouts

`hasDeadline` distinguishes an unbounded wait from an actual unsigned deadline,
including the maximum representable value. Time is the scheduler's monotonic
clock; the event layer does not program hardware.

The dispatcher calls `UmicomKernelSchedulerAdvanceTime()` and
`UmicomKernelEventPump()` between runs. Event operations also pump before doing
new work. A due sleeper can resume and settle its own timeout even if the
dispatcher omitted that separate pump.

At admission an already-signalled event is available even if the deadline is
now. For an existing wait, an elapsed deadline is settled before a later Signal
awards a credit. A signal awarded before the deadline remains successful even
if the callback runs after the deadline. These are decision times, not promises
about when the scheduler will give the callback CPU time.

A cooperative callback that never yields can still stall the dispatcher. This
event layer does not fix that limitation by imposing a fictitious wall-clock
timeout on code that never returns control.

## Cancellation

Wait records contain copied handles and outcomes, not pointers to the waiting
call's output. Pump prunes records whose threads were cancelled, finished or
reaped. It does not write to an abandoned stack and cannot wake a new thread
that reused the old thread's slot with a different generation.

Cancellation after an auto-reset signal was already awarded does not recredit
that signal. An award is a completed event operation even if its callback never
runs again. Services must retain the actual data/condition independently and
must arrange resource cleanup before abandoning a callback.

## Using events with message channels

The acceptance example has an auto-reset readable event and an auto-reset
writable event. It does not need the peer's thread handle.

The producer tries SEND. On `WOULD_BLOCK` it waits for writable, then retries
SEND. After successful SEND it signals readable. The consumer follows the mirror
image: it retries RECEIVE after a readable notification, and signals writable
after consuming a packet. Producer closure signals readable as another reason
to retry and observe peer closure.

Notifications do not reserve packets or queue slots. They can coalesce or be
stale by the time a callback runs; the queue operation remains the authority.
The demonstration transfers 32 copied packets and checks ordering and content
after the producer overwrites its original local buffer.

This is Kernel callback coordination over the unchanged message implementation.
User-mode message calls remain nonblocking. User wait syscalls and process
suspension are planned only after a suitable resumable process scheduler exists.

## Review and normal workflow

Only `CMakeLists.txt` and `kernel/main.c` gain integration lines. All old lines
remain unchanged; nothing is superseded or disabled.

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new main test is `kernel.riscv64.events`. It requires the existing
`kernel.build.current` fixture. Do not remove the incremental build directory.

## Optional native tests

The optional suite uses the previous Linux x86-64 alternate-stack adapter.
It does not require that adapter or Linux to be installed on the normal
Windows-to-RISC-V build machine.

On a Linux x86-64 development host:

```text
cmake -S tests/events -B build/native-events -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_ASM_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-events --parallel 2
ctest --test-dir build/native-events --output-on-failure --no-tests=error
```

UndefinedBehaviorSanitizer is enabled by default. AddressSanitizer is not
claimed because the existing hand-written alternate-stack adapter does not
perform ASan fibre notifications. Its host hardware model is identified as a
model rather than a guest boot.

The release direction, repositories and remaining work are documented in
[KERNEL_AND_OS_RELEASE_ROADMAP.md](KERNEL_AND_OS_RELEASE_ROADMAP.md).
