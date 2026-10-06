# Umicom Kernel — Task-owned standard streams and foreground terminal

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## What a program can now do

The interactive console already runs independent native programs from named
RAM-backed files. Those programs now have an explicit way to read foreground
input and write ordinary output or error output. An empty input does not require
repeated user calls: the program can suspend inside its read and continue when
input arrives. A full output queue applies the same backpressure to a writer.

The shell, scheduler, saved-frame Assembly, user-memory checker, file services
and IPC queues keep their existing implementations. This owner adds copied byte
storage and a terminal adapter; it does not create another process runner or
weaken a memory/interrupt admission check.

The labels stdin, stdout and stderr describe three **native stream selectors**.
They are not hosted C `FILE` objects or the VFS's generation-tagged descriptor
tokens. This distinction is deliberate: the native stream boundary is small,
and descriptor redirection or a POSIX layer can later adapt it explicitly.

## Ownership and execution

```text
foreground user program
     | READ/WRITE, with its own address values
     v
existing trap-origin, instruction-range and call-budget checks
     |
     v
exact task/session stream binding
     |                         |
     | possible immediately    | would block
     v                         v
copy and return           copy request / retain next PC
                               |
                               v
                     existing Assembly retains the frame
                               |
                               v
                     PAUSED but not eligible to execute
                               |
                  input feed / EOF / output drain
                               |
                               v
                     pump writes a0/a1 exactly once
                               |
                               v
                     existing scheduler resumes after ECALL
```

A pending operation is an additional eligibility condition on the existing
PAUSED state. It is not represented as IPC's BLOCKED state and does not pretend
to be an IPC message. The scheduler asks the stream owner whether a paused task
is eligible. Query the stream snapshot to distinguish a timer pause from a
stream wait.

The owner and scheduler must occupy stable, zero-filled Kernel storage. Attach
once before admitting any task. Grant is explicit for each newly created task,
before its first quantum. It grants stdin reading and stdout/stderr writing;
there is no user-supplied owner identity or selectable terminal domain.

Each binding checks the actual task record and exact report pointer. A copied
session containing the same identity does not acquire that task's streams.
Numbers from one scheduler do not select a record in another scheduler.

The implementation is single-hart and serial. Kernel service code is not
pre-empted. No concurrent mapper or interrupt handler mutates these queues.
Public controller operations use the existing allocation-context gate as their
machine-state boundary. That gate is not an SMP lock. The trap-time byte paths
use preallocated storage and checked copies, not the UART, VFS or a callback.

## The native register contract

| Register | On entry |
|---|---|
| a7 | STREAM_READ (40) or STREAM_WRITE (41) |
| a0 | INPUT (0), OUTPUT (1) or ERROR (2) selector |
| a1 | User buffer address |
| a2 | Requested byte count |
| a3 | Zero for waiting behaviour, or NONBLOCK (1) |

On return, **a0 is status and a1 is transferred bytes**. Both are documented
results of these new calls. Existing syscalls retain their original register
contract. The small native program's Assembly wrapper preserves its output-count
pointer on its own user stack before ECALL, then stores returned a1 there.

READ accepts INPUT only. WRITE accepts OUTPUT or ERROR only. Invalid directions,
unknown flags, excessive requests and bad address spans are refused. An ungranted
invocation receives UNBOUND. An authenticated zero-length operation succeeds
without touching its address or changing a queue; direction and flags still
have to be valid.

At most 256 bytes are transferred per call. The old user-copy primitive's
64-byte bound remains unchanged: this layer checks the complete operation and
uses bounded chunks. It does not increase a global copy limit for other callers.

## Reading input

Controller input is copied into a 1,024-byte ring. A feed either accepts all its
bytes or none. This is useful for terminal lines: backpressure cannot turn one
submitted line into an unnoticed shorter request. The controller retains no
pointer after a successful feed.

READ checks the complete requested destination, even if fewer bytes are currently
available. It copies the available prefix up to the requested size and only then
removes those bytes from the ring. A bad second page leaves input and the first
page unchanged.

A nonblocking empty read returns WOULD_BLOCK. A waiting empty read records its
address and length and returns to the dispatcher. Every completion rechecks the
destination before consuming input. A changed invalid mapping completes with
BAD_BUFFER and leaves the input available for a later valid operation.

There is no deadline in this stream ABI. A waiting task remains live until input,
EOF or cancellation supplies progress. Controller code must continue polling or
otherwise servicing input; this delivery does not add an interrupt-driven idle
loop. No user dispatch or system-call count is charged merely for pumping an
unfulfilled wait.

## End of input is a state, not a special data byte

`EndInput` is sticky and idempotent for one task's lifetime. Buffered input can
still be read. Only an empty, ended input returns EOF with zero bytes. An empty
live input is never reported as EOF. Ending stdin does not end stdout/stderr.

READ may return a short successful prefix; a program must use its actual count,
not assume a full request. Byte value zero is ordinary stream data, not an
implicit terminator. The interactive adapter's printable-line policy is separate
from the byte service.

## Writing output and preserving its order

There are eight output records per task. One record contains its selector,
length and up to 256 copied bytes. Stdout and stderr share this ordered queue,
so their accepted write order is retained. No ordering across different tasks
is promised, and a record is not required to contain a whole text line.

A successful WRITE accepts the whole supplied chunk, not a prefix. A full queue
returns WOULD_BLOCK for NONBLOCK or suspends a waiting call. The waiting write
owns a copied payload before suspension; it does not reread a later-modified
user buffer. The pending payload is separate from already accepted records.

Draining returns and removes one copied record. It can occur after the producer
has stopped because no user address is needed. A drain makes space; the next
pump can accept the pending write and fill its saved status/count. Repeated
pumps never duplicate an already completed output operation.

Output is binary at the Kernel service boundary. Safe terminal rendering is a
separate consumer policy. Neither text formatting nor terminal callbacks run
inside the user's trap invocation.

## Stop, drain, collect

Cancellation, exit, fault and quantum/syscall exhaustion end input and abandon
unfinished stream operations. A cancelled pending WRITE is not an accepted
packet and is scrubbed. Packets accepted earlier remain available.

The collector must drain accepted output before reaping the image. Reap refuses
undrained output rather than silently throwing it away. A trusted collector may
instead call Discard on a safely stopped task as an explicit loss policy.
Discard is refused while that task can still execute.

After output is accounted for, stream metadata is cleared and the existing
process destructor performs memory cleanup. A later task in the same slot needs
a new grant; it does not inherit bytes, EOF or stream authority. A stale token
cannot feed the replacement. The scheduler cannot detach a stream owner while
records or tasks remain.

If the architecture adapter cannot prove that machine controls were restored,
controller access and collection remain refused. Ownership is retained for
diagnosis. Corrupt queue counts or selectors similarly cannot drive unchecked
array accesses. This is defensive Kernel bookkeeping, not a security boundary
against malicious machine-mode code that can overwrite both data and metadata.

## Foreground terminal behaviour

The terminal adapter attaches to the existing interactive shell. The normal
shell command editor remains separate from the foreground program's input
editor. Commands are not interpreted while stdin is active.

- Enter submits the edited line followed by a newline byte.
- Backspace/Delete edit; Ctrl-U clears the pending input line.
- Ctrl-D with a nonempty line submits that piece without a newline. Another
  Ctrl-D on an empty line ends input for this invocation.
- Ctrl-C uses the existing supervisor cancellation path, not a user signal ABI.

The existing editor refuses unsupported controls, escape sequences, overlong
lines and reported UART input damage. Rejected input is not passed as a valid
truncated prefix. If the input ring is full, the adapter reports that the whole
line was refused. It does not silently accept part of it.

A command entered as CR/LF cannot donate its trailing LF to the newly started
program. When the foreground program ends, a half-entered input line is not
executed as the next command. Stream input and the old type-ahead barrier are
kept separate.

The terminal drains output between quanta and before collecting a stopped image.
Printable ASCII is displayed normally. LF is rendered as CR/LF; other control
bytes are escaped as `\xNN`. A program cannot submit an escape sequence which
clears the terminal or impersonates the shell's cursor controls. Stderr records
are prefixed with `[stderr] ` at the consumer, not modified in the stream queue.
This label is per write record, not per logical line.

The shell's output callback remains a trusted non-reentrant callback. It is not
an asynchronous host I/O completion interface. Program output, status reporting
and the final prompt all pass through that existing callback in serial order.

## Try it in the interactive image

Build and test through the established preset:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Start the **interactive** image:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-console.elf" `
    -display none `
    -monitor none `
    -chardev "stdio,id=console,signal=off" `
    -serial "chardev:console" `
    -m 128M `
    -smp 1 `
    -no-reboot
```

The QEMU stdio setting leaves Ctrl-C available for guest cancellation instead of
QEMU's terminal-signal termination. See the official [QEMU character-backend
reference](https://www.qemu.org/docs/master/system/qemu-manpage.html).

At `umicom>`, run:

```text
run /bin/umicom-stream-client.elf 0
```

After the program asks for input, type `Transfer approved` and press Enter. The
program writes `received: Transfer approved` to stdout and a separate explanatory
line to stderr, then exits. The shell collects it and returns the prompt.

Run the same command again and press Ctrl-D on an empty input line to exercise
EOF. Use Ctrl-C during another empty read to exercise cancellation.

Other numeric demonstration modes are:

| Mode | Behaviour |
|---|---|
| 1 | Twelve alternating stdout/stderr writes, exceeding one queue's capacity |
| 2 | Direction, bad-buffer and nonblocking-empty refusal checks |
| 3 | Accept one output record, then deliberately fault |
| 4 | Accept output, then spin until cancelled or its existing slice budget ends |
| 5 | Accept one output record and exit normally |

Finish the session with `poweroff`. The existing RAM-only storage and trusted
machine-mode console limitations remain. There is no descriptor redirection,
pipeline, background job, POSIX terminal, login, structured argv/environment or
normal service manager in this update.

## Optional native tests

On a Linux development host with a native C compiler:

```text
cmake -S tests/standard_streams -B build/native-standard-streams -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUMICOM_STREAM_SANITIZERS=ON
cmake --build build/native-standard-streams --parallel 2
ctest --test-dir build/native-standard-streams --output-on-failure --no-tests=error
```

The standard Windows-to-RISC-V workflow does not require these optional tests
or installation of a Linux runtime. Their explicit hardware model is described
in `STANDARD_STREAMS_VALIDATION.md`.

## Next integration boundaries

Structured arguments and environment layout, user-visible terminal descriptor
adaptation and normal boot/service management are future work. Existing scalar
program arguments, file tokens and nonblocking message calls are unchanged.
The old console path also remains usable without attaching a terminal owner;
its old input-discard behaviour is not removed from source.
