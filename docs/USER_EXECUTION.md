# Umicom Kernel — User execution, isolation and checked system calls

## What changes, and what stays in place

The supervisor experiment already runs a small C function in S-mode and returns
on its first trap. This user monitor has a different job. It enters U-mode,
handles a sequence of environment calls, and resumes the user registers after
successful or refused calls. Exit, a protection fault, an exhausted call budget,
or a machine-timer deadline ends the invocation and returns to the saved Kernel
caller.

The established bootstrap, trap handler, physical allocator, Sv39 mapper,
hardware-translation validation and supervisor experiment remain unchanged.
There is no replacement supervisor handler and no new versioned Kernel ABI.

Only the built-in acceptance program is loaded here. There is no ELF user loader,
process scheduler, persistent process manager, or supervisor system-call server.
The service monitor still runs in machine mode. That is an explicit staging
boundary, not a claim that the complete Kernel has moved to supervisor mode.

## The execution path

```text
Machine Kernel setup
    builds two Sv39 roots with different writable physical frames
    records each permitted backing page in Kernel-owned memory
    arms a temporary machine-timer deadline
        |
        v
Private machine entry saves the caller's control state
    installs a RAM-only PMP region and its own trap vector
    clears inherited integer registers and sets MPP=User
    executes MRET
        |
        v
User C payload
    owns only its stack and data views
    requests IDENTITY / COPY through ECALL
        |
        v
Private machine trap entry
    exchanges sp with mscratch before any memory access
    saves the complete integer frame on the machine stack
    calls C policy with trusted machine pointers
        |
        +---- resumable result -> a0 + next user PC -> MRET
        |
        +---- exit/fault/deadline -> restore machine caller -> RET
```

The monitor uses the existing `UmicomRiscvTrapFrame` layout, with compile-time
checks for every shared register and CSR slot. It does not copy the old trap
implementation into the active trap path. The extra entry code is necessary
because this monitor has a separate safe stack and can resume user system calls;
the supervisor experiment deliberately cannot.

## Two roots, the same virtual addresses

Tasks A and B use the same user program and the same virtual stack/data layout.
Their roots, stacks, data frames and protection-test frames are distinct.

Only the immutable executable text pages are shared. They are readable and
executable, with the Sv39 USER bit set, but never writable. Stack/data mappings
are readable and writable, with USER set, but never executable. No global leaf
mapping is used. A fence accompanies each root switch because these early roots
reuse ASID zero.

The acceptance sequence runs A, B, then A again. Each invocation begins at the
entry function with a fresh register context; the task's data allocation remains
alive between invocations. A's counter must reach two while B's remains one.
After the negative tests, another B invocation must update only B's counter.
This proves address-space reentry and data separation. It is not yet suspension
and resumption by a scheduler.

The data-copy source crosses two virtual pages deliberately backed by
non-contiguous physical frames. Passing this test requires consulting each PTE;
adding an offset to the first physical frame is not sufficient.

## Native environment-call contract

The user places the request number in `a7`, arguments in `a0` to `a2`, and executes
ECALL. A resumable call changes only `a0` and advances the saved PC by four bytes.
The other integer registers remain as captured. The ordinary C wrapper may
shuffle its own argument registers before ECALL, as any C wrapper may do.

| Operation | Inputs | Result |
|---|---|---|
| EXIT | a0: user exit value | Ends this invocation; never resumes EXIT |
| IDENTITY | No trusted user input | a0: Kernel-assigned task identity |
| COPY | a0: destination, a1: source, a2: byte count | a0: success or a defined refusal |

COPY is limited to 64 bytes. It validates both complete spans, reads into a
Kernel scratch buffer, then writes the result. This also gives overlapping
spans a well-defined read-before-write behaviour. Unknown calls return a refusal;
they do not panic the Kernel.

These operation numbers are an internal experimental contract, not Linux or
POSIX syscall numbers. The names and evidence describe functionality rather than
development chronology.

## Why a user pointer is not authority

Machine mode could dereference physical memory that U-mode cannot access. The
monitor must therefore do its own checks before copying on the user's behalf.
`UmicomKernelUserMemoryCheck` requires:

* a non-wrapping, canonical nonempty span;
* a present mapping on every touched page;
* USER plus the requested read, write or execute permission;
* an exact match with a Kernel-owned backing-frame/permission record;
* no writable-executable or global mapping in this user environment.

An empty span is a documented no-op and does not dereference either pointer.
A present supervisor-only PTE is refused just as deliberately as a missing page.
A read-only second page must not leave the first page partially modified.

The page tables and ownership records are created by trusted Kernel code and
never exposed as writable user memory. The existing page walker is not hardened
against an arbitrarily corrupted Kernel root pointer. That is outside this
interface's input contract.

The preflight/copy guarantee relies on the present single-hart, non-reentrant
monitor and immutable mappings during a call. Before adding concurrency, the
Kernel must add a mapping lock, pinning or equivalent lifetime protocol. Simply
adding `volatile` would not solve that ownership problem.

## Returning safely from broken user state

A user may leave `sp` pointing at an unmapped page. The private trap vector's first
instruction swaps it with the saved machine context in `mscratch`. Only after
that swap does it store registers. The poisoned-stack EXIT case tests this
ordering directly.

While the C dispatcher runs, the original machine trap vector is temporarily
restored. A bug in Kernel policy must be a Kernel failure, not a second user
trap that overwrites the saved frame. The user vector is reinstalled only when
the monitor is ready to return to U-mode.

Floating-point and vector state are disabled for this integer-only context.
The initial integer registers are cleared rather than exposing values left by
machine code. User counter access is disabled while the payload runs and the
caller's counter permissions are restored afterwards.

## Bounded execution, not a scheduler

A call-count limit stops a program that continually requests services. It cannot
stop a program that makes no calls. Therefore each invocation also borrows a
machine-timer deadline. The busy-loop case makes no further calls and must end
through a real timer interrupt from U-mode.

The platform adds just one query, `UmicomPlatformTimerCompareRead`, using the
existing timer-register locator. Setup saves the old compare value and restores
it after the invocation, along with the original interrupt/delegation state.
The current QEMU CLINT profile uses a 10 MHz timebase; the timeout is 100 ms.

This deadline terminates the invocation. It does not yet enqueue, suspend or
schedule a process. The test also does not claim support for multiple harts,
NMI recovery, arbitrary external programs or a production security policy.

## Runtime acceptance cases

1. Task A executes the checked service sequence.
2. Task B executes the same sequence in its own root.
3. Task A reenters; its persistent counter must not be B's counter.
4. A load from the unmapped stack guard faults with cause 13.
5. A store through a read-only user mapping faults with cause 15.
6. A load from a mapped supervisor-only page faults with cause 13.
7. A user read of supervisor `satp` faults with cause 2.
8. An instruction fetch from a data page faults with cause 12.
9. EXIT with an invalid user stack reaches the safe machine return path.
10. Repeated calls exhaust the monitor's service budget.
11. An endless user loop is stopped by the machine-timer deadline.
12. Task B still completes normally after all those faults and stops.

Each normal run also checks ten copy/refusal cases and an Assembly register
probe. The Kernel independently checks copied physical bytes, protected data,
stack canaries, trap cause/PC/previous privilege, borrowed control state and
final frame accounting. A user-written completion flag is not enough.

The original machine ECALL handler is invoked again before teardown, proving
that the temporary user vector did not replace the normal handler permanently.

## Normal build and test

Use the existing incremental build directory:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new runtime test is `kernel.riscv64.user_execution`. It requires the existing
`kernel.build.current` fixture and is additional to the seven prior tests.

A successful manual run should end with:

```text
user.original-trap-handler=pass
user.frame-accounting=restored
user.completed-cases=12
user-execution-test=pass
UMICOM_KERNEL_USER_EXECUTION_READY
UMICOM_KERNEL_END
```

Expected page faults are checked inside the guest. An unexpected event prints
`UMICOM_KERNEL_FAIL`, not a success marker. The host test timeout also remains
in place to expose an emulation or monitor hang.

## Optional native checks

The independent `tests/user_memory` CMake project compiles the same C allocator,
page walker, copy policy and dispatcher for the host. It runs 24 tests, without
claiming hardware privilege execution. It is useful for ASan/UBSan and checking
copy refusal paths before a QEMU run.

```powershell
cmake -S tests/user_memory -B build/user-memory-native -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/user-memory-native --parallel 2
ctest --test-dir build/user-memory-native --output-on-failure --no-tests=error
```

The optional sanitizer switch is `-DUMICOM_USER_MEMORY_SANITIZERS=ON` on a host
whose compiler provides those runtimes. It is not required for the Kernel build.

## Architectural sources

RISC-V International, Machine-Level ISA: privilege transitions, user ECALL,
`mscratch`, interrupt priority, PMP and machine status.
https://docs.riscv.org/reference/isa/v20260120/priv/machine.html

RISC-V International, Supervisor-Level ISA: Sv39, USER/R/W/X leaf permissions,
`satp` and `SFENCE.VMA`.
https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html

CMake, CTest fixture prerequisites: current-image build dependency.
https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html
