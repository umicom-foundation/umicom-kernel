# Umicom Kernel — Cooperative Kernel threads

## What is different from running a function normally?

A normal C call uses the caller's current stack and runs until it returns. A
cooperative thread has its own stack and can temporarily give control back to
the dispatcher. When selected again, it continues after the same yield or wait
call. Its nested calls and local variables still exist.

The existing user monitor already preserves user registers across ordinary
system calls. This implementation adds a separate, deliberately small Kernel
thread facility. It does not modify that user monitor, the process registry,
the executable loader, the machine trap handler, or the message queues.

These are trusted machine-mode Kernel callbacks. They all share Kernel memory
and authority. This is not a user-process scheduler, a privilege boundary, a
multi-hart scheduler, or timer-driven pre-emption.

## The new ownership boundary

The scheduler owns eight thread records and their stacks. The caller supplies a
callback and a borrowed argument pointer. Neither callback nor argument is
copied. The argument must remain alive until the thread is reaped; cancellation
does not destroy application resources referenced by that argument.

The state sequence is:

```text
EMPTY -- Create --> READY -- RunOne --> RUNNING
                                  /      |      \
                       Yield --> READY   |       return --> FINISHED
                                         |
                              Sleep --> SLEEPING -- time/wake --> READY
                              Wait  --> WAITING  -- wake ------> READY

READY / SLEEPING / WAITING -- Cancel --> CANCELLED
FINISHED / CANCELLED -- Reap --> EMPTY with a new generation
```

A terminal thread is not automatically collected. Query can inspect its result
until the owner calls Reap. A live or suspended stack cannot be reaped.

Reap must run on the dispatcher, not on the stack being freed. It copies the
terminal result, clears the context and the whole stack, and then advances the
slot generation. At generation exhaustion, the slot retires instead of wrapping
to a value that could make an old token valid again.

Thread handles belong to this scheduler domain. They are not process handles,
channel handles or cryptographic secrets. The APIs are Kernel-only and accept
trusted, non-overlapping pointers. A handle is a lifetime check, not permission
to expose this service directly to untrusted user arguments.

## Why the stacks are part of the scheduler

Each record has a 16,384-byte, sixteen-byte-aligned stack array. Sixty-four bytes
at each end contain a known pattern, leaving 16,256 usable bytes.

The current physical allocator returns individual frames. It does not promise
that several successive allocations will be contiguous. Statically owned
stacks avoid inventing that promise or introducing another allocator here.
A static scheduler lives in BSS and is covered by the existing Kernel image
reservation. No allocator behaviour needs changing.

The margins are **canaries, not unmapped guard pages**. The scheduler checks
them and the saved stack pointer at controlled boundaries. A wild Kernel write
can still corrupt memory before that check. These callbacks are trusted; this
mechanism is diagnostic, not a sandbox or overflow prevention.

A later stack allocator can supply independently protected virtual stacks. That
change must preserve ownership and ensure no running stack can be reclaimed.

## A call-boundary context is not a trap frame

The production context switch is in `arch/riscv64/thread_context.S`.

The RV64 lp64 calling convention preserves the stack pointer and s0 through
s11 across an ordinary call. The switch additionally stores the return address,
gp and tp. When it restores another context, `ret` resumes that thread's saved
continuation. A new context instead returns into the bootstrap, which invokes
the C callback and records its eventual completion.

The compiler already accounts for caller-saved temporary and argument registers
at this ordinary call boundary. Saving only this subset would be wrong for an
interrupt that can arrive between arbitrary instructions. Pre-emption needs a
complete interrupted context, explicit interrupt ownership and separate tests.

The current admission helper requires hart zero, Bare `satp`, no enabled machine
interrupt sources, global MIE clear, MPRV clear and inactive floating-point and
vector state. Calling this helper already assumes trusted machine mode because
it reads machine-only CSRs. It does not safely probe an unknown privilege level.

No existing vector is replaced and no timer interrupt is consumed by the
scheduler. Kernel callbacks must preserve that machine-state contract. An
unsafe or corrupted return poisons the scheduler instead of allowing another
context to run or claiming that its stack is safe to reclaim.

## Round-robin selection and the dispatcher's responsibility

`UmicomKernelSchedulerRunOne()` scans the bounded table beginning just after
the last selected slot. It runs one READY continuation and returns when that
callback yields, waits, sleeps or finishes.

The scan is bounded by eight slots. It needs no heap-backed run queue. Runnable
callbacks that cooperate receive turns in round-robin order.

`IDLE` means no thread is ready **now**. It does not mean that all threads have
finished. Snapshot distinguishes sleepers, explicit waiters and terminal owners.
It also reports the earliest sleeping deadline, when one exists.

A caller can limit how many times it calls RunOne. That limits dispatches, not
elapsed time inside a callback. A callback that loops forever without yielding
cannot be stopped by this cooperative scheduler. Do not mistake this budget for
the machine-timer termination boundary used by the existing user monitor.

Only one dispatcher tree may be active. Calling RunOne recursively, including
through a different scheduler, is refused. Otherwise a callback could try to
yield a record while actually running on another scheduler's stack.

## Time and wake-up

`UmicomKernelSchedulerAdvanceTime()` accepts a monotonically increasing tick
value from trusted dispatcher code. A lower value is refused; arithmetic does
not wrap. Every sleeper whose deadline has arrived is made READY before the
next selection.

In the guest validation, the dispatcher supplies `UmicomPlatformTimerRead()`.
The scheduler does not program the timer or execute WFI. A sleeping thread uses
no CPU between dispatches; deciding how the dispatcher waits for the next
hardware event is a separate platform/interrupt integration task.

`UmicomKernelThreadSleepUntil()` returns immediately when its deadline is
already reached. Otherwise the original call returns only after time advancement
or an explicit Wake makes the thread runnable and a later RunOne resumes it.

`UmicomKernelThreadWait()` waits for an explicit wake. Wake is idempotent for a
READY thread but does not accumulate credits for a future Wait. A condition must
be checked and the wait recorded without yielding between those operations.
After waking, the callback rechecks its condition; wake is not a reservation of
the resource it was waiting for.

## The producer/consumer example

The acceptance example creates two Kernel callbacks and uses the existing
message-channel API. The receiver starts without a message and waits. The
producer fills the bounded queue, wakes the receiver, and waits when it sees
`WOULD_BLOCK`. The receiver drains the queue and wakes the producer.

Each successful send is followed by overwriting the producer's original local
buffer. The consumer still receives the original bytes because the existing
queue owns its copy. Twenty-four packets are sent across multiple suspend/resume
cycles. Closing the producer leaves accepted messages available for draining.
Both endpoint references and both thread stacks are explicitly reclaimed.

No yield occurs inside a channel operation or between observing the condition
and recording the wait. That serialised rule avoids a lost wake in this example.
It does not make the unchanged message service SMP-safe or turn user RECEIVE
into a blocking syscall. User-mode message calls still return `WOULD_BLOCK` as
before. Automatic wait registration, user-task resumption and pre-emptive
scheduling require further integration.

## API usage

The primary operations are:

| Operation | Allowed caller | Result |
|---|---|---|
| Initialize | Trusted setup | One stable scheduler domain |
| Create | Dispatcher | New READY callback and handle |
| RunOne | Dispatcher | One cooperative dispatch, or IDLE |
| Yield | Current callback | Suspend and remain READY |
| SleepUntil | Current callback | Suspend until time/wake |
| Wait | Current callback | Suspend until explicit wake |
| Wake | Peer callback or dispatcher | Make waiter/sleeper READY |
| Query / Snapshot | Serialised Kernel caller | Value observations |
| Cancel | Dispatcher | Abandon a non-running continuation |
| Reap | Dispatcher | Collect terminal result and scrub stack |

Cancel does not unwind C frames or run destructors. It is suitable only after
the owner has accounted for any resources the abandoned callback might hold.
Reap does not release a borrowed argument or close a message endpoint for it.

## Run the normal RV64 checks

No setup changes or build-directory deletion are required.

```powershell
cmake --preset riscv64-clang-debug
```

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2
```

```powershell
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new test is `kernel.riscv64.cooperative_threads`, using the existing
`kernel_current_image` fixture. The suite now contains twelve tests including
the build fixture.

Run manually with the established command:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
```

The new section ends with:

```text
threads.messages-received=24
threads.message-wait-resume=pass
threads.original-trap-handler=pass
threads.machine-state=unchanged
threads.frame-accounting=unchanged
threads.completed-cases=6
threads.completed-checks=<count>
kernel-threads-test=pass
UMICOM_KERNEL_COOPERATIVE_THREADS_READY
UMICOM_KERNEL_END
```

The check count can depend on how often the dispatcher polls the machine clock.
Addresses and reserved-frame counts can also move as the Kernel image grows.

## Optional native checks

The optional suite uses a **test-only Linux x86-64 System V context adapter**.
It runs the same C scheduler on actual alternate host stacks. It is not an
x86-64 Umicom Kernel boot target and does not validate RISC-V machine registers.

```text
cmake -S tests/threads -B build/native-threads -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-threads --parallel 2
ctest --test-dir build/native-threads --output-on-failure --no-tests=error
```

These commands are for a Linux x86-64 developer environment, not a replacement
for the normal Windows/RV64 commands above. The suite rejects unsupported hosts.

UndefinedBehaviorSanitizer is enabled by default. AddressSanitizer is deliberately
not used: the hand-written alternate-stack adapter does not implement ASan's
fibre-switch notification protocol. The existing message-policy suite can still
run independently with its address and undefined-behaviour sanitizers.

## Source integration and preservation

Only the top-level CMake file and `kernel/main.c` receive insertions. Existing
source lines remain unchanged and in their original order. The new build
fragment owns all new source registration and its test. Existing platform,
trap, MMU, supervisor, user, executable, registry and channel source files are
not replacement files in this delivery.

Nothing is superseded. No historical block, alias, comment or new disabled copy
is manufactured for this addition.

## References

The original implementation uses the published ABI and privileged architecture:

* RISC-V ELF psABI, integer calling convention and stack alignment:
  https://riscv-non-isa.github.io/riscv-elf-psabi-doc/
* RISC-V machine architecture (mstatus, mie, privilege and extension state):
  https://docs.riscv.org/reference/isa/priv/machine.html
* CTest fixture dependency semantics:
  https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html

## Next architectural boundary

This addition establishes resumable Kernel call stacks and explicit lifetime.
It does not silently change the existing run-once user process model. A future
user scheduler must retain complete interrupted user registers, bind service
ownership per runnable task, make IPC waits race-safe, and separately qualify
pre-emption and address-space switching. Those changes should build on the
working context and ownership tests rather than replacing them wholesale.
