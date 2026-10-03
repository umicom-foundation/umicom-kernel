# Umicom Kernel — Process registry and rights-checked handles

## Why this layer exists

The executable loader gives a program its own frames, page tables, entry point
and terminal report. The architecture adapter can run that image and return
when it exits, faults or reaches its deadline. Those pieces are already useful,
but passing a mutable `UmicomKernelProcess *` between every future service would
make ownership difficult to enforce.

The registry adds one layer above them. It keeps process owners at stable
addresses and hands out references that identify both an owner and permitted
operations. It does not replace the loader, user monitor, page walker or trap
code. There is no new privilege transition in this implementation.

A registry call follows this route:

```text
Kernel-selected owner + handle + requested operation
                         |
                         v
             validate token and owner
                         |
                         v
                 check handle rights
                         |
                         v
              stable loaded-process owner
                         |
                         v
       existing loader / execution / destruction API
```

This is the Kernel-side foundation for later services and process management.
It is not yet exposed through user system calls, and it is not a scheduler.

## Identity, handles and pointers mean different things

A **process identity** names one loaded program instance. The registry assigns
identities monotonically within its own lifetime. Zero is reserved. Once the
identity counter is exhausted, it refuses further admission rather than
wrapping to an identity that might still be remembered.

A **handle** identifies one reference to an object. One process can have several
handles. Each reference has an owner and a rights mask. Two handles can have
different rights while observing the same process state and terminal report.
Duplicating a handle does not duplicate executable pages or writable data.

A **pointer** is an address used by Kernel implementation code. The registry
never returns a process pointer in its public query results. Queries return
value snapshots, without physical addresses, page-table roots or backing-page
pointers.

This distinction is why closing one handle does not necessarily destroy the
program. A different handle may still own a reference to it.

## Owner-scoped tokens

The token is an unsigned 64-bit value:

```text
63                         32 31                           0
+----------------------------+------------------------------+
| slot generation            | handle-table index + 1       |
+----------------------------+------------------------------+
```

The low half is bounds checked before indexing the table. The high half must
match the generation recorded in that slot. A closed token does not work after
the slot is reused: the generation changed.

The token itself is not secret, random or cryptographically authenticated.
Guessing or copying another owner's token does not bypass the owner check.
The authority is the matching, live Kernel-owned table record, not the integer
alone. A guessed token for another reference already belonging to the same
owner does not create rights that owner did not already possess.

Owner identities passed to this API are trusted Kernel context. A future syscall
must derive the caller's owner identity from its execution context, not accept
an `owner` field supplied by a program. This update deliberately leaves the
existing syscall ABI unchanged until that binding is implemented.

Tokens are local to a registry domain. Different registries can issue the same
numeric token. The caller must select the correct trusted registry; tokens are
not globally portable. A live registry is never copied, reset or reinitialised.
It records its own address so an accidental structure copy is refused rather
than used with stale interior process pointers.

### Generation exhaustion

The final generation is usable once. Closing that handle retires the slot
permanently. Wrapping the counter to one would make an old token valid again,
so exhaustion spends capacity rather than resurrecting authority.

This is a deliberately conservative policy. Future expansion can use a wider
generation or a different domain design, but must preserve the stale-token rule.

## Rights

| Right | Permitted operation |
|---|---|
| `UMICOM_PROCESS_RIGHT_QUERY` | Read a value snapshot of process state and its terminal result. |
| `UMICOM_PROCESS_RIGHT_RUN` | Invoke the existing run-once READY-image execution path. |
| `UMICOM_PROCESS_RIGHT_DUPLICATE` | Create another reference with the same or fewer rights. |
| `UMICOM_PROCESS_RIGHT_TRANSFER` | Together with DUPLICATE, issue a reference to another owner. |

Unknown rights bits are rejected. They must not silently acquire meaning when a
future operation is added.

`UmicomKernelProcessRegistryRestrict()` only removes rights from the selected
handle. It can reduce the mask to zero. A zero-rights reference remains closable
by its owner, so giving up authority cannot prevent cleanup.

`Duplicate()` requires DUPLICATE. The requested rights must be a subset of the
source handle's rights. The new token is bound to the same owner.

`Grant()` requires both DUPLICATE and TRANSFER. It issues a **new token** bound
to the receiving owner, again with no more rights than the source. It neither
revokes nor silently moves the sender's reference. A later revocation mechanism
would need a separate design; it is not implied here.

Closing a reference is permitted without a separate CLOSE bit. Owner validation
still applies. A QUERY-only observer can release its reference but cannot run
the program or turn itself into a more powerful handle.

## Stable objects and bounded capacity

The current registry contains eight process slots and thirty-two handle slots.
Each owner may hold at most eight handles. These are explicit admission limits,
not performance claims or a substitute for future configurable quotas.

The registry is stored in zero-initialised Kernel memory, normally static BSS.
Its process slots do not move, because the existing process implementation keeps
borrowed pointers into its own owner structure. No heap or new allocator is
required.

When a limit is reached, admission fails before starting another image load.
Nothing is evicted to make room. Duplicating a handle uses a table slot and a
reference count, not another set of physical image frames.

All access is serialised by the caller on one hart. The busy flag prevents
recursive use during `ProcessRun`; it is not an atomic lock and does not provide
SMP safety. The existing process adapter additionally enforces its own machine
state and single-invocation conditions.

## Admission is a commit point

`Create()` first checks arguments, handle capacity, the owner's quota and process
capacity. It then calls the existing `UmicomKernelProcessCreate()`.

Only after loading succeeds does the registry publish the first handle. A failed
ordinary load leaves the output token unchanged and the original loader rolls
back its frames. A parser refusal is not converted into an empty successful
process.

An unusual rollback failure is different: the process owner may still contain
frames. The registry keeps that object occupied with zero published references.
It can be counted by `SnapshotRead()` and retried by the Kernel-only `Reap()`
maintenance operation. Losing the object because no user handle was returned
would turn a recoverable ownership record into an invisible leak.

All API buffers and owner objects are Kernel-controlled, stable and
non-overlapping with registry storage. These interfaces are not a validation
boundary for arbitrary hostile Kernel pointers or arbitrary memory corruption.

## Running and observing a process

`Run()` checks the owner and RUN right before calling the existing architecture
adapter. It retains the reentry guard throughout execution, so a recursive
service call cannot close or mutate this registry while the invocation uses it.

The underlying READY image can run once. A second RUN on an exited or faulted
image is refused; no invisible restart, fork or image clone occurs.

A program fault or timer deadline is a terminal process result, not necessarily
an internal Kernel error. The query snapshot records the process state, identity,
reference count, page count, exit value, trap cause and system-call count.
`exitValue` is meaningful only for an EXITED image.

A return whose machine state was not verified leaves `quiesced` false. The final
handle cannot be closed in a way that frees that potentially active image. It
remains available for diagnosis; this registry does not guess how to recover
from an unsafe architecture return.

## Closing references and handling cleanup errors

For a non-final reference, Close invalidates only that handle and decreases the
object's reference count. Other owners' references keep working.

For the final reference, the order is deliberately different:

1. Prove that the process is not running and is quiesced.
2. Ask the existing destructor to remove page tables and scrub/free backing pages.
3. Only after success, clear the object slot and invalidate the handle.

A destructor refusal leaves the final handle alive. Its generation is not spent,
and Query can still expose CLEANUP_REQUIRED. Retrying Close is possible after a
real cause has been corrected. There is no forced free on an error path.

`CloseOwner()` is Kernel lifecycle administration. It closes references owned by
one principal, including references to several different processes. Other owners'
grants remain valid. It can make partial progress; the output count reports only
successful closes before an error. It is not a transaction that resurrects already
closed handles on failure.

`Reap()` retries only unpublished, unreferenced objects retained after failed
admission. It does not sweep live references or override a running-state refusal.

## Validation

The new guest validation reuses `umicom-diagnostic.elf`. Its six cases cover:

- malformed-image refusal without a published handle;
- owner checks, read-only observation, explicit sharing and final-reference lifetime;
- stale-token rejection while a replacement program faults;
- timer termination followed by a fresh successful program;
- per-owner and global handle-table capacity;
- bounded process admission without eviction.

The test then calls the original machine ECALL handler again and checks that
all temporary process and page-table frames returned to the physical baseline.

Successful serial evidence ends with:

```text
registry.original-trap-handler=pass
registry.frame-accounting=restored
registry.completed-cases=6
registry.completed-checks=<number of checked conditions>
process-registry-test=pass
UMICOM_KERNEL_PROCESS_REGISTRY_READY
```

The guest may report `exited`, `faulted` and `timed-out` as expected terminal
states. The test checks the actual state and trap cause before accepting them.

The native suite compiles the real registry, loader, physical allocator,
virtual-memory implementation and user-memory checks. Only the architecture
`ProcessRun` boundary is substituted. That substitution proves routing and
reentry policy, not RISC-V execution. Test-only metadata mutations deliberately
exercise cleanup retention and invariant failures; they are clearly separated
from normal API examples.

## Recurring build and test commands

From the existing Kernel checkout:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new `kernel.riscv64.process_registry` test uses the existing
`kernel_current_image` fixture. There are ten tests in the main project,
including its incremental-build guard. No build-directory deletion is needed.

Optional native tests use a separate host build directory and do not consume
the RISC-V toolchain file:

```powershell
cmake -S .\tests\process_registry -B .\build\native-process-registry -G Ninja -DCMAKE_C_COMPILER="C:/msys64/ucrt64/bin/clang.exe" -DUMICOM_REGISTRY_SAMPLE="$PWD/build/riscv64-clang-debug/programs/umicom-diagnostic.elf"
cmake --build .\build\native-process-registry --parallel 2
ctest --test-dir .\build\native-process-registry --output-on-failure --no-tests=error
```

The default native configuration does not require sanitizer runtimes. The Linux
validation described in the companion evidence document enabled AddressSanitizer
and UndefinedBehaviorSanitizer explicitly. Windows-native execution was not run
in the delivery environment.

## Design references and limits

Windows' documented object-handle model is a useful example of keeping object
references and rights distinct from raw pointers:
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/object-handles

CMake documents the setup-failure behaviour used by the existing build fixture:
https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html

These are design references, not imported implementations. The registry source
is Umicom code, and it does not implement Windows handle compatibility.

This update does not add user-visible handle syscalls, automatic owner cleanup
on program exit, a global PID service, revocation trees, IPC, concurrent process
execution, scheduling, process suspension, fork or a production security sandbox.
Those features can build on the ownership rules here without changing the
already-working executable loader into a process-manager monolith.
