# Umicom Kernel — Copied message channels and native services

## What this adds

The process registry can name a loaded program, limit the rights of its handles,
and retain its memory until explicit destruction. Message channels give separate
programs a way to exchange bytes without receiving pointers into each other's
memory.

This implementation is an owned, bounded FIFO transport. A sender calls the
native SEND service; a receiver calls RECEIVE. The Kernel copies the payload,
assigns sender metadata, checks rights and owns the queued bytes. There is no
shared-memory mapping or remote process pointer in a message.

The runtime demonstration loads two instances of a separately linked
`umicom-message-exchange.elf`. They run through the existing process registry,
ELF loader and user execution monitor. The sender completes first. Its process
image is destroyed before the receiver runs. Accepted messages remain available
because their bytes belong to the channel, not the sender's old frames.

This is not a scheduler. Empty or full live queues return `WOULD_BLOCK` now;
they do not put a thread to sleep. The program can handle that result, finish,
or later use a scheduling/wakeup interface when one exists.

## Source ownership

The existing implementation is not replaced. Three integration points change:

| Existing file | Added responsibility |
|---|---|
| `CMakeLists.txt` | Include the new build fragment. |
| `kernel/main.c` | Invoke message validation after process-registry validation. |
| `kernel/user_monitor.c` | Recognise the four new calls after the existing privilege, PC and call-budget checks. |

Every original line in those files remains unchanged and in order. The original
platform, trap, MMU, user-entry Assembly, supervisor, allocator, loader and
registry files are not part of the replacement overlay.

The new source is grouped by responsibility:

* `message_abi.h` gives the Kernel and native program shared fixed-width layouts.
* `message_channel.h` and `message_channel.c` own endpoints, references and queues.
* `message_service.h` and `message_service.c` bind trusted execution context and
  validate user buffers before calling the channel API.
* `message_validation.c` combines the existing process services with the new
  channel service for real guest acceptance.
* `programs/message_exchange/` is the independent native program, not Kernel text.
* `tests/message_channels/` exercises the actual portable policy and memory code
  on the host without claiming that a host test entered RISC-V user mode.

## A channel contains two incoming queues

```text
endpoint A                           endpoint B
incoming queue A <----- SEND B        incoming queue B <----- SEND A
        |                                    |
    RECEIVE A                            RECEIVE B
```

A sender never writes directly to the other program's buffer. SEND copies into
the other endpoint's queue. RECEIVE copies from that queue into a checked buffer
of the receiving program.

Each channel has two endpoints. Each endpoint can have several owner-bound
references. Closing a reference reduces its endpoint's reference count; it does
not close that endpoint until the last reference is gone.

When an endpoint closes completely, messages waiting **for that endpoint** are
scrubbed and discarded: nobody can receive them. Messages it already sent to
its peer remain queued at the peer. That surviving peer can drain them and then
observe `PEER_CLOSED`.

Closing the last reference to both endpoints scrubs the entire channel before
its slot can be used by another pair.

## Limits and admission

The current fixed Kernel-owned domain has:

| Resource | Limit |
|---|---:|
| Channel pairs | 8 |
| Handle references | 32 |
| References owned by one principal | 8 |
| Incoming messages per endpoint | 4 |
| Payload bytes per message | 256 |

These are admission bounds, not a throughput claim. Full queues do not evict
old packets. Exhausted tables do not steal another owner's slot. A same-owner
pair needs two free references in that owner's quota, checked before either
reference is published.

The domain lives at one stable address and is initialised from zero-filled
storage once. Reinitialising even an empty domain is refused: resetting slot
generations could revive stale handles. A copied domain is rejected by its
self-address check. Its visible fields permit static allocation, not arbitrary
client mutation.

## Handles, identity and rights

A token contains a slot index plus a generation. Its integer value is neither
a secret nor a Kernel address. A request must name a live generation and the
correct owner, and that reference must carry the required rights.

When a slot closes, its generation advances. At the maximum generation it is
retired permanently rather than wrapping. Old tokens cannot thereby select a
later endpoint occupying the same slot.

Process handles and message handles are separate domains. The same integer can
exist in both without conferring interchangeable authority. The syscall number
and the trusted binding select the intended service; user data cannot select a
Kernel registry pointer.

| Right | Permitted operation |
|---|---|
| `SEND` | Enqueue a copied payload at the peer. |
| `RECEIVE` | Inspect and consume the local incoming packet. |
| `QUERY` | Read a value snapshot of rights and readiness. |
| `DUPLICATE` | Issue another reference with equal or fewer rights. |
| `TRANSFER` | Together with DUPLICATE, grant a reference to another owner. |

Restriction only removes rights. Zero-rights references remain closable so a
caller cannot trap itself into retaining a resource merely by giving up
permissions. Closing requires ownership but not an additional CLOSE right.

Creation, grants and owner-wide cleanup remain Kernel-side admission and
lifecycle operations. A user can SEND, RECEIVE, QUERY and CLOSE already granted
references. A token embedded in payload bytes does not automatically grant its
receiver authority; the normal owner check still applies.

## How a user call obtains its owner

The service does not accept an `owner` register argument.

Before running a registered process, Kernel admission queries its assigned
process identity, binds a message domain to that identity, and invokes the
existing registry run operation. The C trap dispatcher receives the trusted
`UmicomKernelUserSession` created by the execution path. The service requires
that session's identity to match the binding.

This also means a process-registry owner's identity and the process's own
identity are not interchangeable. The acceptance harness uses registry owners
9001 and 9002 to manage process references, then uses the **assigned process
identities** to own the message endpoints.

An unbound invocation cannot use message services. Nested binding is refused.
Unbinding happens after a normal exit, protection fault or deadline, before the
caller interprets the result or starts another program.

The binding is intentionally single-hart and serial. One message domain must
use one trusted process-identity namespace. Do not bind identities from two
independent process registries which can issue the same numeric identity into
the same message domain. A concurrent scheduler will need per-execution-context
bindings and explicit domain identity; this implementation does not pretend
that a global pointer supplies those guarantees.

## Native calling convention

The existing register convention remains: service number in `a7`, arguments in
`a0` through `a2`, and status returned in `a0`. All other saved integer registers
remain intact. Every recognised message call is still subject to the existing
user-origin check, executable PC validation, next-PC validation, call budget and
machine timer deadline.

| Call | a0 | a1 | a2 |
|---|---|---|---|
| `MESSAGE_SEND` | endpoint handle | user source address | payload length |
| `MESSAGE_RECEIVE` | endpoint handle | user destination address | total buffer capacity |
| `MESSAGE_QUERY` | endpoint handle | user information structure | zero |
| `MESSAGE_CLOSE` | endpoint handle | unused | unused |

The four call numbers are 16 through 19. The established EXIT, IDENTITY and COPY
numbers and behaviour remain unchanged. The monitor extension is compiled only
when the new service is linked. Independent native tests for the older minimal
monitor can still build it without this optional service.

A successful RECEIVE writes this fixed-width native RV64 little-endian prefix:

```text
byte 0    : sender identity (64 bits)
byte 8    : channel sequence (64 bits)
byte 16   : payload byte count (64 bits)
byte 24   : payload bytes, not necessarily text
```

Only the prefix plus actual payload bytes are written. The caller does not need
a full 280-byte buffer for a smaller packet, but insufficient capacity refuses
the operation rather than truncating it. The Kernel assigns sender and sequence;
putting other numbers in the payload cannot replace this metadata.

Sequence numbers advance across sends in both directions. Each individual
incoming queue is therefore ordered but may have gaps. Zero is not issued.
Exhaustion refuses a later send rather than wrapping to an old sequence.

QUERY writes five 64-bit fields: rights, queued count, next payload size,
readiness flags and local endpoint reference count. It exposes no physical
address, page-table root or Kernel pointer.

## Copying is bounded and checked

The older checked-copy helpers accept at most 64 bytes per call. That contract
is unchanged. The message adapter first preflights the complete larger span,
then uses the same helpers in chunks of at most 64 bytes.

SEND stages the source in private Kernel storage before enqueuing. If any page
is absent, not user-accessible, wrongly backed or lacks read permission, nothing
is queued. Changing the source after success cannot change the queued packet.

RECEIVE peeks without consuming, checks capacity, validates the complete user
destination, copies its header and payload, and only then consumes the expected
front sequence. An absent or read-only second page is refused before writing
the first page. A short destination or bad mapping leaves the packet available
for retry.

These atomicity statements rely on the documented serialisation rule: no other
consumer, callback or page-table mutation may run between validation and commit.
They are not SMP guarantees. The sequence check detects an accidental stale
commit, but it is not a concurrent queue-reservation protocol.

## Readiness and nonblocking behaviour

`READABLE` means the reference has RECEIVE and a packet is waiting.
`WRITABLE` means it has SEND, the peer is alive, there is queue capacity, and
sequence space remains. `PEER_GONE` can be present together with READABLE while
accepted packets remain to be drained.

Readiness is a snapshot. It must not be treated as a promise about a future
concurrent operation. The current service does not implement waits, wakeups,
polling descriptors or scheduler queues.

## Runtime acceptance journey

The guest performs six cases:

1. Rights checks, an attenuated grant, source-buffer independence and draining
   a message through a surviving receiver reference after the sender closes.
2. Independent traffic in both directions and discarding a dead receiver's queue.
3. Two actual registry-loaded user programs exchanging four packets with native
   SEND/RECEIVE/QUERY/CLOSE calls.
4. A separately loaded program refusing message use without a Kernel binding.
5. Explicit endpoint-owner cleanup after a loaded program faults.
6. Explicit endpoint-owner cleanup after a loaded program reaches its deadline.

The user producer tries a forbidden receive, a bad source and an excessive
length; fills four queue slots; observes backpressure; changes its source bytes;
closes its endpoint; and verifies that its old token is invalid.

The Kernel destroys that producer's process pages. The consumer then checks
peer closure and queued readability together, tries a forbidden send, an
undersized destination and an overflowing destination, drains the four packets
in order, verifies their authenticated sender and original copied bytes, and
closes its endpoint. Both source and destination span user page boundaries.

Finally, the harness proves that the original machine trap handler still works,
that all temporary process frames have been reclaimed, and that the channel
domain contains no remaining references or packets.

Expected concluding markers are:

```text
message.user-exchange=pass
message.original-trap-handler=pass
message.frame-accounting=restored
message.completed-cases=6
message-channels-test=pass
UMICOM_KERNEL_MESSAGE_CHANNELS_READY
UMICOM_KERNEL_END
```

## Normal Windows workflow

The development tools are already installed. Keep the existing incremental
build tree and use:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run each command after the preceding command succeeds. CTest registers eleven
main tests, including `kernel.build.current` and the new
`kernel.riscv64.message_channels`. The new test requires the existing build
fixture; it does not bypass stale-image protection.

For manual execution:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
```

Check `$LASTEXITCODE` immediately afterwards; successful completion returns zero.

## Optional native tests

This is a separate host executable, not a RISC-V guest. It compiles the actual
channel, service adapter, C trap dispatcher, allocator, mapper and checked-copy
implementations. No substitute queue or fake copy routine is used.

```powershell
cmake -S .\tests\message_channels -B .\build\native-message-channels -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build .\build\native-message-channels --parallel 2
ctest --test-dir .\build\native-message-channels --output-on-failure --no-tests=error
```

There are 72 named cases, including 2,000 successive channel lifetimes, queue
saturation, generation/sequence exhaustion, rights reduction, owner separation,
cleanup, preflight failures and the real C monitor's PC/privilege/budget guards.
On hosts with sanitizer runtimes, add `-DUMICOM_MESSAGE_SANITIZERS=ON` when
configuring this separate test tree. That switch is optional on Windows; the
normal Kernel build does not require a sanitizer runtime.

## Boundaries which remain open

There is no blocking IPC, scheduling, handle attachment, shared memory, automatic
reply routing, production namespace policy or multi-hart synchronisation. Process
exit does not silently close message references: the lifecycle owner calls
`UmicomKernelMessageCloseOwner()` when appropriate. This explicit boundary is
exercised after faults and deadlines rather than hidden inside the old registry.

The result is narrower and useful: independent native user programs can exchange
Kernel-owned copied packets through checked calls, without modifying the working
loader or architecture execution machinery.
