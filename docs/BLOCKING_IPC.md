# Umicom Kernel — Blocking messages and resumable scheduler waits

## What a waiting call means

A nonblocking receive tells a program that the queue is empty. Repeating that
call in a tight loop wastes user instructions, system calls and scheduling
quanta. A waiting receive instead keeps the program alive but not runnable. Its
peer can run; the receiver continues at the instruction after the same ECALL
when a packet arrives, the peer closes or its deadline expires.

The existing SEND, RECEIVE, QUERY and CLOSE calls are unchanged. Two additional
operations opt into waiting: MESSAGE_SEND_WAIT and MESSAGE_RECEIVE_WAIT. They
are implemented above the existing queues, page walker, checked-copy helpers,
resumable frame entry and executable owner. No second Assembly context switch
or message queue implementation is introduced.

## Where this sits

```text
separately loaded user program
              |
              v
existing origin / instruction / call-budget validation
              |
              v
checked waiting message service
     | available         | WOULD_BLOCK with a wait requested
     v                   v
finish now          record copied request and next PC
                         |
                         v
                 existing Assembly saves the frame
                         |
                         v
                    BLOCKED task
                         |
          peer progress / close / deadline
                         |
                         v
          complete into the retained frame, then PAUSED
                         |
                         v
         existing scheduler and Assembly resume user code
```

Only user instructions are pre-empted. Queue operations, wait registration,
copy validation, task cancellation and scheduler bookkeeping remain serial
Kernel work on hart zero with interrupts disabled. No mapper runs concurrently.
The arguments about atomic preflight/copy and lost wakeups depend on that
contract; this is not an SMP-safe or interrupt-handler wait queue.

## Native call contract

The register convention remains the existing native ECALL convention:

| Register | Meaning |
|---|---|
| a7 | MESSAGE_SEND_WAIT (20) or MESSAGE_RECEIVE_WAIT (21) |
| a0 | Endpoint token on entry; status on return |
| a1 | User source or destination address |
| a2 | Send payload bytes or total receive capacity |
| a3 | Relative timeout in platform timer ticks |

A send has 1–256 payload bytes. A receive accepts 24–280 bytes of capacity,
including three metadata words. Packets are never truncated. Results reuse the
existing message statuses; the additional TIMED_OUT result is 0x100.

Timeout zero means one immediate attempt: WOULD_BLOCK returns without sleeping.
An explicit all-ones value means no deadline. Other values are finite and cannot
exceed 10,000,000 ticks. That is one second for the current 10 MHz QEMU profile,
not a portable nanosecond duration. Adding a finite interval must not overflow.
A future platform-time API should expose physical units without changing the
meaning of existing native operation numbers.

The new monitor branch recognises exactly the two waiting calls before the
ordinary service branches. Both paths follow the existing origin, executable-PC
and budget checks. No existing operation number is reassigned. The original
message branch is not rewritten and continues handling nonblocking calls.

## A domain belongs to one scheduler

Create stable, zero-filled UmicomKernelUserScheduler and UmicomKernelUserIpc
objects. Initialise the scheduler, then attach the IPC owner before admitting
any task. Neither object is copied, reset, reinitialised or detached while live.

The IPC owner contains a private message domain. Different scheduler domains can
issue the same numeric process identity, so sharing an unqualified global
message namespace would confuse authority. Private domains prevent that
collision. At most four tasks can own pending requests in this scheduler.

Kernel admission calls UmicomKernelUserIpcConnect with scheduler-issued task
tokens. The service obtains identities from those records rather than accepting
identities supplied by a user program. UmicomKernelUserTaskSetArgument can then
supply each new program with its endpoint token. It accepts only a READY task
which has never executed; it cannot overwrite a resumed register or syscall
result. Both records still use the established executable loader and backing
page catalogue.

Before each quantum, the adapter binds the original nonblocking message service
to the selected task and this domain. After every admitted or refused hardware
entry, that binding is released. A conflicting pre-existing binding is refused,
not replaced. An ordinary run-once process has no waiting-service binding and
receives SERVICE_UNBOUND for these operations.

The process-registry handle namespace remains separate. There is no user syscall
for creating this scheduler domain, attaching arbitrary channels or selecting
another process's identity. Binding registry authority to scheduled task
lifetimes belongs to subsequent process-supervision integration.

## Register once, complete once

A waiting request stores its generation-tagged task token, authenticated
identity, endpoint, virtual buffer address, length, validated next instruction,
registration order and optional absolute deadline. A pending SEND also stores
its own bounded payload bytes. None of these records is a pointer to an old
machine trap frame or a later callback output on an abandoned stack.

The service first preflights the user span and tries the actual queue operation.
It registers a wait only for WOULD_BLOCK with a nonzero timeout. There is no
yield between testing the condition and publishing this record. Under the
single-hart Kernel contract, a peer cannot run in that interval.

The saved PC advances by four bytes once, at registration. A timer pause does
not advance a PC, but this is an already accepted ECALL. The existing Assembly
copies the invocation frame into the stable task owner. Its placeholder result
is not exposed while the task is BLOCKED. Completion fills a0, clears the wait
record and only then publishes the task as PAUSED and eligible to resume.
It does not execute a second syscall or charge another call-budget entry.

The invocation which reached the wait still spends one slice. Waiting itself
spends none. A task which reaches its total slice budget at registration becomes
exhausted; it does not gain more execution by repeatedly waiting. Existing
cumulative call limits also continue across wakeups.

## Data and permission checks

SEND copies its payload before suspension. Changes to the original user buffer
after admission cannot change those pending bytes. The pending snapshot is
scrubbed when completed or cancelled.

RECEIVE preflights its destination at admission, and again before deferred
completion. It first peeks at the actual packet, checks capacity and validates
the entire span before writing its first byte. It copies through the existing
bounded user-memory API, then consumes the exact packet sequence. There is no
concurrent mapper or consumer between those steps in this implementation.

A missing or read-only second destination page refuses the operation without
partially changing the first page or consuming the packet. An undersized
receiver similarly leaves the packet queued. Revoked rights, invalidated tokens
and peer closure are rechecked through the existing queue APIs rather than
remembering authority from an old successful query.

Kernel-owned wait records and continuation PCs are checked before queue side
effects. A reversed clock or corrupt retained ownership poisons the scheduler.
That requires diagnosis, not forced reclamation of possibly live frames.

## Deadlines, order and idle dispatch

RunOne pumps pending requests before choosing runnable work. Pending requests
are considered by registration order, not by their task-table slot. Completing
one request can make another queue operation possible in the same pump. A task
is never dispatched solely to ask again whether its condition has changed.

At initial admission, an immediately available operation can finish before a
wait is registered. Once registered, an elapsed deadline wins over readiness
observed at a later pump. Completion is a decision point: a successful result is
not undone merely because the task is scheduled after that deadline.

Registration order applies among existing pending waits at a pump. It is not a
reservation or a global fairness promise against nonblocking operations that
run between pumps. Those original operations retain their existing semantics.

IDLE means no task is runnable, not that all task records can be freed. The
snapshot reports waiting count and the earliest finite deadline. The dispatcher
must continue pumping time, or eventually arrange a timer-driven idle wakeup.
This update does not install a hardware idle loop. Its deadline acceptance test
polls time in Kernel code; the blocked user does not busy-spin or spend quanta.
Infinite waits consume only their existing bounded descriptor and image until
a condition, close or explicit cancellation resolves them.

## Terminal state and cancellation

EXIT, a user fault, total slice exhaustion, call-budget exhaustion and explicit
cancellation close that identity's references in the attached private domain.
Accepted messages already in the peer's incoming queue survive. The peer can
drain them and then observe PEER_CLOSED. Other owners' handles are not closed.

Cancellation removes an uncommitted request before releasing its endpoints. A
pending full-queue SEND cannot later enqueue when a replacement task reuses its
slot. A completed SEND is different: its packet already belongs to the queue,
so cancellation after completion does not retract it.

A BLOCKED task remains a live continuation. Reap refuses it. Cancel makes it
terminal; the existing explicit image destructor then clears and returns its
frames. This does not unwind user stack frames or supply general process-parent
cleanup. External domains still require their own lifecycle supervisor.

## Runtime acceptance

The independently linked umicom-blocking-exchange.elf uses a page-crossing
buffer and nested stack locals. The five guest cases cover:

1. An empty-queue receiver and a full-queue sender both really suspend, then
   transfer twelve packets through normal round-robin scheduling. Start counters
   stay one; packet order and stack locals survive.
2. A finite receive deadline completes without another user dispatch or call.
3. A faulting peer closes its endpoints and wakes the surviving receiver.
4. A peer exhausting its slice budget produces the same bounded cleanup.
5. A cancelled blocked task is reaped and its slot reused. Old tokens fail;
   the replacement also exercises invalid-buffer and zero-timeout refusals.

The original machine trap path, timer compare, machine controls and physical
allocation accounting are checked after all cases. The ordinary Kernel emits
UMICOM_KERNEL_BLOCKING_IPC_READY only when this guest code reaches the end.
Native test results are not substituted for that marker.

## Normal Windows workflow

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Keep the incremental build directory. The new main test is
kernel.riscv64.blocking_ipc and requires the existing current-image fixture.
There is no additional tool installation for this update.

## Optional native tests

These use the earlier scheduler test's host RAM, ELF and CSR model. Only
privileged entry and hardware time/state are modelled; the C scheduler, loader,
allocator, page walker, trap dispatcher, message queue and wait service are the
actual implementations. The shared test source remains unchanged.

```text
cmake -S tests/blocking_ipc -B build/native-blocking-ipc -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_BLOCKING_IPC_SANITIZERS=ON
cmake --build build/native-blocking-ipc --parallel 2
ctest --test-dir build/native-blocking-ipc --output-on-failure
```

Compiler and sanitiser availability vary by host. These optional native tests
are separate from the normal Windows-to-RISC-V cross-build.
