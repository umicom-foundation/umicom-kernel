# Umicom Kernel — Timer-driven resumable user scheduling

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## A timer stop is not necessarily the end of a program

The existing run-once process service treats its deadline as a terminal result.
That remains useful for bounded service checks, and it is not replaced here.

The new scheduler gives a timer stop a different meaning only for an explicitly
bound scheduling invocation: save the continuation, restore the machine caller,
and let that caller dispatch another ready task. When the first task is selected
again, its saved instruction pointer, integer registers and private address
space are restored. The executable entry point is not called again.

```text
Machine dispatcher                  User Task A              User Task B
        |                                |                         |
        +---- restore A ---------------->|                         |
        |                           CPU-bound work                 |
        |<--- timer: save A -------------+                         |
        +---- restore B ------------------------------------------>|
        |                                                   CPU-bound work
        |<--- timer: save B ---------------------------------------+
        +---- restore A ---------------->|                         |
        |                         continue interrupted work        |
```

The timer causes the return to the dispatcher even if user code never makes a
system call. Selection is a bounded round-robin scan in C. Kernel C code itself
is not pre-empted: admission, service calls, lifetime updates and selection remain
serial machine-mode operations. `RunOne()` returns after one admitted invocation;
the caller decides whether and when to dispatch again.

## What is reused, and what changes

The original executable inspector, loader, frame allocator, page walker,
checked-copy helpers and C syscall policies are reused. The existing process
registry, cooperative Kernel-thread scheduler and events are unchanged.

Four existing files receive insertions only:

| File | Addition |
|---|---|
| `CMakeLists.txt` | Include the new build fragment. |
| `kernel/main.c` | Run the new guest acceptance after interrupt ownership. |
| `kernel/user_monitor.c` | Recognise a timer stop for the explicitly bound session. |
| `arch/riscv64/user_execution.S` | Accept an optional retained frame and reuse its existing register save/restore body. |

No old line is removed, rewritten or disabled. In particular, there is still
one production user-register save/restore body, not a copied alternative which
could drift from the original monitor.

The original two-argument `UmicomRiscvUserExecute()` explicitly selects the old
run-once behaviour. The new three-argument `UmicomRiscvUserExecuteFrame()` receives
a trusted Kernel-owned frame. Its pointer is kept in a separate aligned extension
above the original machine context; none of the original frame offsets changes.

## Saved state is owned, not borrowed from an expired stack

An asynchronous interrupt can arrive between ordinary instructions. Saving only
callee-saved registers, as a cooperative C call permits, would lose caller-saved
values which user code is still using. This path retains all 31 mutable integer
registers, `mepc`, and the captured trap information.

The original local trap frame is copied into the stable task record before the
machine invocation releases its stack. On a later dispatch, the saved values are
copied back into the shared restoration path. There is no pointer to a frame on
an old invocation's stack.

A timer does not request instruction skipping. Its captured `mepc` is preserved
exactly. The existing ECALL path still advances only its validated four-byte
instruction and returns the service result in `a0`.

The saved stack pointer is treated as a user register, not a Kernel pointer.
A user program may execute EXIT with a zero stack: the private trap entry still
uses its machine landing storage. Conversely, a continuation PC must name an
executable user halfword before resumption.

Before restoring a continuation, C rejects unsupported privilege, floating-point
or vector state. Assembly reconstructs its allowed status from the hart's real
fields and selects integer U-mode. The captured status is evidence, not permission
to restore arbitrary privilege bits.

The user `gp` and `tp` values are saved before the machine pair is restored for C
service dispatch. A dummy store-conditional to a private reserved zero word clears
a possible load reservation before another context runs. This does not claim
an SMP atomic protocol or floating/vector context support.

## Task lifetime and the lower process owner

A task owns an existing `UmicomKernelProcess`, which in turn owns its loaded frames
and page tables. The two state machines answer different questions:

* Task READY/PAUSED means the continuation may be dispatched.
* Lower process RUNNING with `quiesced == false` means that this image still backs
  a live execution lifetime and must not be restarted or destroyed.

The lower process remains RUNNING from task admission through pauses. This is
intentional: passing that lower owner to the old run-once or destructor API cannot
accidentally restart or free the paused program. It is private task storage, not
an independently published registry object.

```text
Create -> READY -> RUNNING -> PAUSED -> RUNNING -> ...
                       |                    |
                       +------ terminal ----+

READY/PAUSED -- Cancel --> CANCELLED
Terminal -- explicit Reap --> EMPTY, with generation retained
```

Terminal outcomes include EXITED, FAULTED, EXHAUSTED, CANCELLED and ERROR. Reaping
uses the existing destructor: remove translations, scrub backing frames, and free
the ownership only after safe return is known. Reaping an eligible live task is
refused. Cancellation abandons a paused continuation but does not unwind user code.

A rare loader rollback failure retains an unpublished record for explicit
`UmicomKernelUserSchedulerReapRetained()` retry. A machine-control restoration
failure poisons the scheduler and leaves its images pinned for diagnosis. It is
not converted into a successful user fault or automatically freed memory.

Task tokens are slot/generation values local to their scheduler domain. Slot
exhaustion retires rather than wraps the generation. Process identities are also
local and monotonic within this new trusted domain; they are not a global identity
allocator shared with the separate existing process registry.

## Timer ownership and quantum semantics

The architecture adapter first calls the existing interrupt-ownership admission
check. It refuses an open managed critical section, an owned source, the wrong
hart, active translation, live interrupt sources or unsupported machine state.
It also requires the timer compare register to be parked, rather than stealing
another disabled source's still-programmed deadline.

Only then does it synchronously borrow the timer, set the quantum deadline and
enter U-mode. While that private invocation is active, no other Kernel dispatcher
or ownership client runs. The adapter restores the compare value, vector/scratch
pair, delegation, translation, PMP and counter-access policy before reporting a
pause or terminal outcome to the caller.

This is the same narrow machine-mode borrowing model as the existing bounded
user runner. It does not create a general interrupt callback registry. The
ordinary machine timer handler is not modified to perform task selection.

A timer interrupt observed before the new deadline may be delayed notification
from an earlier compare value. The bound session resumes its unchanged frame in
that case. A bounded retry count rejects a persistent early-interrupt storm.
A due timer marks the invocation paused, not exited. An unbound session still
uses the original terminal-deadline semantics.

The API accepts 1,000 to 1,000,000 platform ticks per invocation. These are timer
ticks, not retired instructions. The guest test uses 100,000 ticks, or ten
milliseconds on the selected 10 MHz QEMU timer, to leave emulated startup time
before testing the CPU-bound loop. This is not a real-time scheduling guarantee;
shorter quanta may be spent on entry overhead on a slow execution environment.

Each task also has a total admitted-slice budget, at most 4,096. Exhausting it
terminates that task with an explicit budget outcome. The syscall counter and
copy accounting persist across slices, so pre-emption cannot reset the original
system-call limit. An ordinary refused entry spends neither a slice nor a
round-robin turn.

## Public Kernel operations

| API | Responsibility |
|---|---|
| `UmicomKernelUserSchedulerInitialize` | Initialise stable zero-filled storage once. |
| `UmicomKernelUserTaskCreate` | Load a private executable and prepare its first integer frame. |
| `UmicomKernelUserSchedulerRunOne` | Select and run one ready or paused task for a bounded quantum. |
| `UmicomKernelUserTaskQuery` | Return values, not a mutable process pointer. |
| `UmicomKernelUserTaskCancel` | Stop a non-running live continuation. |
| `UmicomKernelUserTaskReap` | Explicitly release a terminal image and invalidate its token. |
| `UmicomKernelUserSchedulerReapRetained` | Retry unpublished loader-cleanup records. |
| `UmicomKernelUserSchedulerValidate` | Check the bounded domain's ownership invariants. |

Four tasks fit in one statically allocated domain. All inputs and output pointers
are trusted Kernel storage, non-overlapping with its live records. Do not copy,
reset or edit a live scheduler. These are Kernel service calls, not a new user ABI.

## The separately linked acceptance program

The build produces `programs/umicom-scheduled-work.elf`. Its executable bytes are
embedded as a data span and loaded through the existing ELF loader, just like the
earlier independent diagnostics.

Its private observation page is deliberately at virtual address `0x00600000` in
each instance. The test harness may inspect that page only through the existing
checked user-memory functions while the task is paused. This fixed address is a
diagnostic contract, not a public process ABI.

Two CPU-bound instances keep distinct register values and nested C locals live.
Their loops do not call a yield service. The Kernel releases their test gates only
after both have actually progressed and each has accumulated at least two timer
pauses. Opening A's gate must not change B's writable page. A start counter catches
a restart masquerading as resumption.

Two Assembly probes use different scratch registers. Together they explicitly
check every mutable integer register; nested volatile C locals and the original
stack value are checked too. After release, both programs use the unchanged COPY,
IDENTITY and EXIT services, and their terminal values are checked.

The six guest cases are:

1. Two CPU-bound images progress in round-robin order, then resume and exit.
2. A read-only-store fault ends one task while its neighbour keeps its context.
3. An endless loop reaches its slice budget while a second program exits with
   a zero stack pointer.
4. A paused task is cancelled and reaped; its old token cannot select a replacement.
5. Repeated system calls reach the existing cumulative call budget.
6. An open interrupt critical section refuses entry before a timer or task is changed.

The harness also verifies the original machine ECALL path and returns allocated
frames to their pre-test counts. The exact pre-emption and check counts are not
fixed timing contracts.

## What is deliberately not included

Kernel callbacks and service code remain non-pre-emptive. There is no SMP,
floating-point/vector context, priority scheduling, realtime guarantee or automatic
background dispatcher. Blocking user IPC, sleeping user tasks and external device
interrupt routing remain future work.

The new domain is not yet integrated into the existing process-registry handle
surface. The old registry and run-once service remain unchanged. Message services
are likewise not automatically bound to scheduled tasks in this delivery; a future
supervisor must coordinate an authenticated message domain around each dispatch
and clean up external references on termination. Cancellation here releases no
external IPC handle by implication.

The machine remains a cumulative acceptance image which exits after its tests.
It is not yet an interactive or production operating system.

## Normal Windows workflow

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new main test is `kernel.riscv64.user_scheduling`. It requires the existing
current-image fixture. Keep the incremental build directory.

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
```

Successful output ends with `UMICOM_KERNEL_USER_SCHEDULING_READY` and the existing
`UMICOM_KERNEL_END`. Check `$LASTEXITCODE` immediately; the expected success is zero.

## Optional native tests

On a separate native development build, not the RISC-V cross-build directory:

```text
cmake -S tests/user_scheduling -B build/host-user-scheduling -G Ninja -DCMAKE_C_COMPILER=clang -DUMICOM_USER_SCHEDULING_SANITIZERS=ON
cmake --build build/host-user-scheduling --parallel 2
ctest --test-dir build/host-user-scheduling --output-on-failure --no-tests=error
```

This suite compiles the actual scheduler, C admission/restoration adapter,
classifier, loader and C dispatcher. Only the architecture primitives and user
execution are modelled. Passing native tests does not mean RISC-V timer entry or
register restoration has executed. See `USER_SCHEDULING_VALIDATION.md`.

## Architectural references

* RISC-V machine-level architecture: trap PC, interrupt privilege rules, timer
  pending semantics, return-state rules and load-reservation considerations:
  https://docs.riscv.org/reference/isa/priv/machine.html
* RISC-V integer calling convention: why the cooperative call-preserved subset
  is not sufficient for an arbitrary interrupted user instruction:
  https://riscv-non-isa.github.io/riscv-elf-psabi-doc/
* CTest required fixtures and setup failures:
  https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html
