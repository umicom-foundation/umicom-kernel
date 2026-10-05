# Umicom Kernel — Process supervision and coordinated cleanup

## Why this service exists

The loader owns an executable's frames. The scheduler owns its continuation.
The attached IPC service owns endpoint references and pending operations. None
of those mechanisms alone says who should collect a stopped program's result,
or what should happen to its children when their parent disappears.

`UmicomKernelProcessSupervisor` supplies that policy above the existing owners.
It does not implement another ELF loader, trap entry, context switch, frame
allocator or message queue. A task stays at its original stable address inside
the embedded scheduler from admission until explicit collection.

A future Umicom System Manager can use this as a Kernel-side mechanism for its
Master Controller / Slave Controller service relationships. A service restart
policy, persistent service configuration and a user-visible spawn/wait ABI are
not implemented here.

## Three different moments in a process lifetime

Stopping, closing communication and releasing memory are intentionally separate:

```text
ready / timer-paused / IPC-blocked program
                  |
        exit / fault / budget / cancellation
                  |
                  v
existing scheduler ends execution and closes task-scoped IPC references
                  |
                  v
supervisor captures a value-only terminal report
                  |
                  v
apply parent policy; adopt children or stop the dependent subtree
                  |
                  v
collector explicitly asks to release the terminal image
                  |
        +---------+------------------+
        |                            |
     success                      cleanup error
        |                            |
 publish result once         retain handle and cached report
 retire record               retry only the incomplete teardown
```

A terminal report is not an automatic free. The image remains available for
inspection and consumes a bounded task slot until its owner collects it.
Conversely, an idle scheduler is not proof that all programs exited: blocked
programs can still own their continuations and private frames.

## Parent identity is authority, not a user-selected argument

Zero is the **guardian principal**. It belongs to trusted Kernel administration,
not a loaded program. Every program identity comes from the existing scheduler.

The direct parent may query, cancel, collect and configure admission for its
child. A sibling cannot act as the child's parent. A grandparent does not acquire
ordinary direct-child authority automatically. The guardian may perform explicit
administration across the domain. `CollectAny(guardian)` still scans only the
guardian's direct children; explicit `Collect` provides broader administration.

The `caller` and `parent` parameters of these C interfaces are trusted Kernel
context. They must not be copied from syscall arguments. This update adds no
syscall which accepts an identity supplied by a program. The service is not a
security boundary against malicious machine-mode C code.

A future user syscall must derive the caller's principal and trusted domain from
its execution session before invoking these operations. Neither guessing a
number nor copying a token from another domain is authentication.

The supervised handle reuses the existing scheduler token, including its slot
generation. It is not the original run-once process registry's handle domain.
The old registry remains available unchanged, but its tokens cannot be passed
to this service as interchangeable process capabilities. Unifying those public
handle domains is a separate integration decision, not a hidden compatibility
alias in this implementation.

## Choosing the child-lifetime policy

A process's admission specifies what to do with its descendants when it stops:

| Policy | Effect |
|---|---|
| `UMICOM_CHILDREN_ADOPT` | Move direct children to the guardian without cancelling their work. |
| `UMICOM_CHILDREN_CANCEL_TREE` | Record stop requests for the current descendant tree, cancel its live continuations, then adopt surviving records for collection. |

For example, a report-generation controller may own dependent workers which
should stop when it faults. A deliberately independent logging service may
instead be adopted by the guardian. Neither choice implies that the child's
memory should disappear before its result has an owner.

`birthParent` records the original relationship for diagnosis. The mutable
`parent` records current collection authority. Adoption changes only the latter;
it does not renumber the child, restart it, reset its syscall budget or replace
its stack. A former parent cannot use its old identity to reclaim an adopted
child after that parent has stopped.

`StopTree` is an explicit administrative operation. It records every currently
related descendant before cancelling anything. This matters when an intermediate
child uses adoption policy: reparenting that child first must not let its
current grandchildren escape the already accepted tree-stop request.

A child which was adopted in an earlier completed transition is no longer part
of that old ancestor's current tree. Historical provenance is not continuing
kill authority. Parent links always point towards an older identity; bounded
validation rejects missing parents, repeated identities and cycles.

## IPC cleanup follows the existing scheduler

The supervisor owns one scheduler and one attached private IPC domain. It routes
fresh channel admission through `UmicomKernelUserIpcConnect`; it does not edit
queue records or duplicate their permission logic.

When execution stops, the existing scheduler discards that task's pending wait
and closes only its endpoint references. Accepted outgoing packets belong to
the peer's queue and can survive the sender's image collection. A blocked peer
can become ready with a packet, a timeout or `PEER_CLOSED`.

Cancelling a blocked task first discards its pending operation. Only afterwards
can its image become eligible for collection. No pending receiver pointer may
remain aimed at pages which a replacement image can allocate.

The supervisor pumps existing IPC readiness after family changes. Its `RunOne`
method pumps before and after the established user runner. Ordinary callers must
use that wrapper, not bypass it by directly dispatching its embedded scheduler.
This is how an observed parent failure is settled before another child quantum
is selected.

## Terminal collection

`Query` is a value-only read. The public result contains identities, states and
execution observations, not a process pointer, page-table root or physical
address. A cached terminal completion is not rewritten by later pumping.

`Collect` requires a terminal record whose family policy has been settled. It
asks the scheduler to reap the image. Only a successful lower teardown copies
the cached result to the caller and removes the supervisor record. If teardown
fails, the output remains unchanged and both the token and evidence survive.
A second successful collection of the same handle is therefore impossible.

`CollectAny` is a nonblocking Kernel operation. It chooses a direct terminal
child in record-slot order, not chronological exit order. `WOULD_BLOCK` means
there are children but no collectable result now. `NO_CHILDREN` means there are
no direct children at all. This is not yet a blocking user `waitpid` syscall.

A cancellation has a supervisor reason rather than a fabricated hardware fault.
The result distinguishes a direct request, a parent's stop policy and shutdown.
An actual fault retains the original cause. `exitValue` is meaningful only for
an exited program and is set to zero in other terminal classifications.

## The page-table cleanup repair

Failure-injection testing found a pre-existing partial-destruction problem.
`UmicomKernelVirtualAddressSpaceDestroy` correctly disconnected freed child
tables, but left `pageTableFrames` and `mappedPages` describing the original
hierarchy until the final successful return. If a later release failed, a retry
then rejected the remaining hierarchy because those counts no longer matched.

The repair is additive. Before releasing each leaf table it counts the live
mappings while that table is still owned. After a successful free and detach it
subtracts those mappings and the released table from the public counts. Each
successful intermediate-table release also updates the table count. A failed
release leaves its edge and counters untouched.

The old loops, refusal checks, disconnects and final success reset all remain.
Nothing is disabled or removed. Mapped data frames still belong to the process
owner; this change only maintains the page-table hierarchy's accounting.

The native test refuses every frame-release position in a representative
terminal image, including intermediate tables, the root and data pages. It then
retries and checks the original result and final allocator accounting. This is
retryability under that tested release-failure contract, not recovery from
arbitrarily corrupted tables or failing physical RAM.

## Shutdown and failed admission

`BeginShutdown` permanently closes admission and requests cancellation of all
live work in this supervisor. It is idempotent. It does not discard reports,
restart services or implicitly collect images. The guardian drains terminal
results afterwards. The same live domain cannot be reset or reinitialised to
reuse its old generations.

A normal loader refusal publishes no child handle. An unusual rollback failure
is retained by the lower scheduler as an unpublished owner. The snapshot counts
these records separately; `ReapRetained` retries their cleanup without inventing
a successful process admission. Its count preserves partial-progress reporting.

If machine-state restoration or IPC ownership is unverified, further dispatch
and reclamation are refused. Read-only diagnosis remains possible where the
owner structure is intact. Poisoned state is not cleared by an administrative
reset; unsafe roots remain owned for diagnosis.

## Capacity and caller obligations

The underlying scheduler's four-task limit remains unchanged. Uncollected
terminal results consume slots just like live images. Admission does not evict
another child or silently discard its evidence to create room.

Store the supervisor in zero-filled static Kernel memory. Its embedded owners
contain interior pointers; never copy, relocate or reinitialise it. All arguments
and outputs must be valid, non-overlapping Kernel spans. Calls are made between
quanta on hart zero, with Kernel interrupts disabled. The active flag guards
reentry; it is not an SMP lock.

Only existing lower owners mutate task, frame, queue and waiter records. The
supervisor uses their APIs for lifetime changes, plus read-only embedded-owner
metadata for invariant checks and unpublished-failure diagnostics. Test code
may arrange a selected slot or corrupt a field explicitly; production callers
must not imitate those test-only operations.

## Guest acceptance

The added test is `kernel.riscv64.process_supervision`. It runs the existing
separately linked diagnostic and blocking-message programs. There is no new
Assembly entry and no synthetic guest success path.

The six cases check direct-parent authority and single collection; cancellation
of descendants after a parent fault while a sibling survives; adoption of a
blocked child; explicit subtree cancellation; a supervised copied-message
exchange whose producer is reclaimed before final consumer completion; and
one-way shutdown with retained evidence.

Successful completion ends with:

```text
process-supervision.sibling-isolation=pass
process-supervision.orphan-adoption=pass
process-supervision.ipc-cleanup-and-delivery=pass
...
process-supervision.original-trap-handler=pass
process-supervision.machine-state=restored
process-supervision.frame-accounting=restored
process-supervision.completed-cases=6
process-supervision-test=pass
UMICOM_KERNEL_PROCESS_SUPERVISION_READY
```

## Recurring Windows workflow

No new tool installation, source reset or build-folder deletion is needed.
After merging from a clean working tree:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run commands separately and stop after a failed configure or build. The new
runtime test requires the existing current-image fixture; the original tests
remain registered unchanged.

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
$LASTEXITCODE
```

After the build, tests and guest execution pass:

```powershell
git status
git add -A
git diff --cached --stat
git diff --cached --numstat
git commit -m "feat(kernel): add process supervision and coordinated cleanup"
git push
git status
```

## Optional native checks

On a native Linux development host with Clang and Ninja:

```text
cmake -S tests/process_supervision -B build/native-process-supervision -G Ninja -DCMAKE_C_COMPILER=clang -DUMICOM_SUPERVISION_SANITIZERS=ON
cmake --build build/native-process-supervision --parallel 2
ctest --test-dir build/native-process-supervision --output-on-failure --no-tests=error
```

These tests execute the real C ownership, scheduler, loader, queues, page walker
and copy code. The architecture entry, CSRs and clock are explicit host models.
They do not boot the Kernel or execute the diagnostic's RISC-V instructions.
See [validation evidence](PROCESS_SUPERVISION_VALIDATION.md) for the exact
qualification boundary and the before/after cleanup-failure result.
