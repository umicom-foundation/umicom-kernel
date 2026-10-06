# Umicom Kernel — Process-owned descriptors and checked file services

Author: Sammy Hegab, Umicom Foundation. MIT licence.

## From a Kernel filesystem to a program-visible service

The existing VFS can resolve paths, open descriptions, share positions and call
RAMFS. Those C interfaces accept trusted Kernel pointers. Giving a user program
those pointers would bypass the memory boundary which the loader and execution
monitor already enforce.

This layer gives an admitted scheduled task its own VFS client. A program submits
a copied request through ECALL. The Kernel selects the client from the actual
execution session, not from a principal or client address supplied by the program.
A descriptor identifies one reference in that client's table; it is not a physical
address, a global file identity or a secret.

The existing VFS, RAMFS, object cache, page walker, checked-copy implementation,
Assembly entry and process supervisor are reused without modifications. Only the
scheduler, its declaration, the C call dispatcher and the main build/validation
integration gain additive hooks.

## Why the filesystem does not run inside the user trap

When the machine trap calls C, the current user root and quantum source have not
yet been restored to the caller's normal state. The object-cache admission check
intentionally refuses that borrowed environment. File creation or growth may
allocate, so simply calling VFS inside the trap would either fail or require
weakening an important existing restriction.

Instead, an accepted file call follows this route:

```text
user program: FILE(request, result)
                  |
                  v
existing origin, call-budget and executable-PC checks
                  |
                  v
copy request; validate complete outputs; snapshot path/WRITE data
                  |
                  v
advance saved ECALL PC once; retain the request in Kernel storage
                  |
                  v
existing Assembly saves the user frame and restores the machine caller
                  |
                  v
existing slice adapter verifies root, timer and control-state restoration
                  |
                  v
VFS operation in the ordinary allocation-permitted Kernel context
                  |
                  v
copy result; set retained a0; make task PAUSED
                  |
                  v
later dispatch resumes at the instruction following that ECALL
```

There is no second architecture context switch, and no redefinition of when
allocation is permitted. Completion performs the VFS call before RunOne returns;
the next selection can run a peer before resuming the caller.

Each captured file operation spends one existing scheduler slice. It is not
counted as a timer pre-emption. The syscall count increments once in the original
monitor and is not incremented again by completion. A request admitted on the
final available slice is completed, then the task is exhausted and its descriptors
are closed; it receives no further user instructions. This is not unlimited
filesystem work hidden outside the lifetime budget.

## Stable owners and explicit admission

Allocate a zero-filled `UmicomKernelUserFiles` at a stable Kernel address, then
attach it to a scheduler before admitting tasks:

```c
UmicomKernelUserFilesAttach(&files, &supervisor.scheduler, &vfs);
```

The attachment opens a zero-rights anchor client. That reference prevents an
unrelated caller from unmounting the filesystem while the service remains
attached, even when there are no task descriptors yet. The mounted filesystem
must outlive the attachment. There is one file attachment per scheduler.

Attaching does not grant authority. For each fresh READY task with no executed
slices, trusted admission chooses a ceiling:

```c
UmicomKernelUserFilesGrant(&files, task,
    UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
```

A task without a grant can execute normally, but FILE returns SERVICE_UNBOUND.
There is no user call which attaches a different filesystem or supplies its own
principal. Grant cannot be repeated to enlarge a running program's authority.

The service allocates each VFS client through the existing object cache. Client
storage is zero-filled on allocation; the client remains at that address for its
entire lifetime. Closing all descriptors ends the VFS client lifetime, then the
allocation ticket releases the object. Reusing that cache slot creates a new
object lifetime, not a reset of a still-live VFS client.

The descriptor ceiling applies across the mounted namespace. CREATE and REMOVE
are therefore broad authority. No credential system, path sandbox, file ACL or
implicit parent/child descriptor inheritance is introduced. Use a read-only grant
when broader namespace mutation is inappropriate.

## The native register contract

The new service number is `UMICOM_USER_CALL_FILE` (32). Established EXIT, IDENTITY,
COPY and message operation numbers remain unchanged.

| Register | Input |
|---|---|
| a7 | FILE |
| a0 | Address of `UmicomKernelFileRequest` |
| a1 | Address of `UmicomKernelFileResult` |
| a2 | Exactly 48 request bytes |
| a3 | Exactly 16 result bytes |

The request consists of six explicit 64-bit words: operation, descriptor, address,
bytes, argument and options. On return, a0 contains status. A completed backend
request also writes `{ status, value }` to the result buffer. Other integer
registers remain as captured, apart from the expected PC continuation.

An admission refusal always reports through a0. The result buffer is unchanged
when the request fails admission; it must not be interpreted as a fresh result.
An invalid result address cannot be written merely to report that it is invalid.
Programs must initialise their request/result records and check a0 first.

Ordinary results reuse `UmicomKernelVfsStatus`. The file boundary adds
BAD_USER_BUFFER (0x200), SERVICE_UNBOUND (0x201) and TOO_LARGE (0x202). These are
status values, not pointers. Unknown operation values or nonzero unused fields
are rejected instead of acquiring accidental meaning.

| Operation | Request-specific fields | Result value after backend completion |
|---|---|---|
| OPEN | address=path; bytes=length; argument=descriptor rights; options=append 0/1 | New descriptor on success |
| CLOSE | descriptor only | Zero |
| READ | descriptor, address=destination, bytes=0..4096 | Actual transferred prefix |
| WRITE | descriptor, address=source, bytes=0..4096 | Actual transferred prefix, including a partial-I/O error |
| SEEK | descriptor, argument=absolute offset | Zero |
| RESIZE | descriptor, argument=new length | Zero |
| QUERY | descriptor, address=output, bytes=32 | 32 on success |
| DUPLICATE | descriptor, argument=same or reduced rights | New descriptor on success |
| CREATE | address=path, bytes=length, argument=FILE or DIRECTORY | Zero |
| REMOVE | address=path, bytes=length, argument=FILE or DIRECTORY | Zero |
| READ_DIRECTORY | descriptor, address=output, bytes=96 | 96 on success |
| REWIND_DIRECTORY | descriptor only | Zero |

All unused request fields are zero. OPEN does not implicitly create, truncate or
normalise a path. These behaviours remain those of the existing VFS.

Metadata sent to a program contains four explicit words: identity, kind, bytes
and maximumBytes. A directory reply contains a 64-byte name followed by that
metadata. Structure-size assertions verify the wire shapes. Copying individual
fields avoids publishing native padding or backend pointers.

## Copying and partial I/O

The service checks the complete request and result spans before any VFS effect.
Paths are explicit-length input, excluding the terminator. The maximum accepted
length is 255 bytes. An embedded NUL is refused; the Kernel appends a private
terminator to the copied path before passing it to the unchanged VFS parser.

WRITE snapshots at most 4,096 bytes before deferral. Modifying the original source
or request after capture cannot change which bytes the pending operation uses.
The request, path and data are not borrowed user pointers. Rejected or completed
scratch payloads are scrubbed.

READ, QUERY and READ_DIRECTORY validate the complete output before calling the
backend. The output data span may not overlap the result record. It is checked
again after returning to the dispatcher, before opening a handle, advancing a
position, enumerating a directory or mutating the namespace.

The existing checked-copy primitive's small-copy limit remains unchanged. The
adapter preflights a complete span, then uses that primitive in bounded chunks.
No user address is cast directly into a physical pointer. All mapping checks
refer to the selected task's existing memory view.

RAMFS may write a prefix before running out of frames. The result preserves the
backend error and actual byte count rather than silently converting it to full
success or pretending that an error means zero progress. A read returns only the
bytes actually produced, never uninitialised scratch padding. Seeking, duplicate
positions, append behaviour, namespace grammar and directory CHANGED results all
remain VFS policy, not separately reimplemented file-service rules.

These guarantees assume serial Kernel execution and unchanged mappings during
preflight, backend work and copy-out. They do not solve concurrent remapping,
DMA mutation or malicious machine-mode backend code. An internal copy-out failure
after backend effects is not represented as a successful rollback.

## Process stop, faults and cancellation

The scheduler closes a granted client's descriptors when a task exits, faults,
exhausts its dispatch or syscall budget, or is cancelled. This also covers a
blocked IPC task and the special case where an IPC wait consumes the final slice.
Timer-paused and IPC-blocked tasks retain their descriptors while still live.

Stopping a task does not remove its file names or destroy the shared filesystem.
It releases only that client's references. An unlinked file can be reclaimed
when the last such pin is gone. Another process's descriptors and positions are
not closed as a side effect.

The file owner records both task generation and trusted identity. A recycled
scheduler slot cannot inherit a previous client's rights. Descriptor integers
remain local to their respective clients: the same numeric token can identify
different descriptions in two independently admitted clients. That is expected,
not cross-client authority.

Cleanup can fail. ClientClose reports progress and retains the remaining pins;
the allocation reference is freed only after complete client closure. RunOne
retries terminal cleanup before selecting work, and Reap also requires cleanup
before releasing program memory. A failed close cannot be bypassed by recycling
a task slot. If machine restoration is unverified, the poisoned scheduler retains
both program memory and file ownership for diagnosis; no safe-free claim is made.

After every task has been reaped, `UmicomKernelUserFilesClose()` returns cache
frames and closes the anchor client. An interrupted close remains CLOSING for
retry. Successful completion detaches the service. It does not unmount the VFS or
close RAMFS; those owners retain their established independent lifetimes.

## Guest demonstration

`programs/file_client` is linked separately as `programs/umicom-file-client.elf`.
Its ordinary file operations reach the actual FILE boundary; it never calls VFS
on a Kernel stack.

Two loaded instances use separate descriptor clients. A writer creates a journal,
writes and reads across a virtual-page boundary, shares an open position through
a reduced-rights duplicate, tests a refused destination, resizes, enumerates and
unlinks the journal while it is still open. It deliberately leaves three
descriptors open on return. A read-only reader observes a shared input file but
cannot write it or create names.

The following instances fault after opening a file, spin until their slice budget
is exhausted, or are cancelled after an OPEN has completed. Each case checks that
the client record is gone before process collection. An ungranted replacement
cannot inherit the old client's descriptor namespace. Final teardown verifies
physical-frame accounting and machine-state restoration.

The Kernel still runs cumulative self-tests and exits QEMU. This source update
does not add an interactive prompt, UART input, persistent storage, a filesystem
server in user space, blocking device I/O, `exec(path)` or user-selected spawn.
Those are subsequent integration steps rather than implied properties of FILE.

## Recurring workflow

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run QEMU with the same `virt,aclint=off`, one-hart, 128 MiB configuration used by
the preceding updates. The new acceptance marker is
`UMICOM_KERNEL_FILE_SERVICES_READY`. The build fixture remains required. There
is no reason to delete the existing incremental build directory.

## Optional native tests

On a supported native development host with Clang:

```text
cmake -S tests/file_services -B build/native-file-services -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_FILE_SANITIZERS=ON
cmake --build build/native-file-services --parallel 2
ctest --test-dir build/native-file-services --output-on-failure --no-tests=error
```

The native suite links the real C service, scheduler, dispatcher, VFS, RAMFS,
object cache, loader and memory helpers. It reuses an existing architecture model;
it does not execute RISC-V instructions. The file allocation gate is deliberately
unavailable while the model is inside a trap, so it does not validate the new
service by making a forbidden allocation context artificially legal.
