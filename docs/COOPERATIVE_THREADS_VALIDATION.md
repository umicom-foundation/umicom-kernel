# Cooperative Kernel threads — Delivery validation

## Baseline and integration

Baseline: `5f335b9a645109bc8f73fc0c3160f905af2dcf22`

The live repository records the committed message-query initialisation fix.
This delivery includes that baseline's complete top-level CMake file and
`kernel/main.c`, with additions only. It does not replace the corrected message
program or any existing library implementation.

| Existing file | Added lines | Removed/replaced lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| kernel/main.c | 7 | 0 |

The baseline blob hashes used for the preservation comparison are:

```text
CMakeLists.txt 32d26a1df51e8f4552c1ba6d42d4bab383aa2e95
kernel/main.c 3a97a651e0999b5dd4e6f381995150da67556bf5
```

A sequence comparison verifies every original line remains unchanged and in
order. Added source contains no abbreviated project identifiers or development
batch/version labels. Previously committed material outside these additions
is not rewritten.

## Build validation

The complete Kernel, diagnostic executable and message executable were
cross-built with Clang/LLD 17, targeting `riscv64-unknown-elf`, in Debug and
Release. The existing warning policy, including `-Werror`, was retained. The
existing C23 CMake setting selects this compiler's `-std=c2x` spelling.

The linked Kernel remains ELF64 little-endian RISC-V with entry `0x80200000`.
All new context, bootstrap, register-probe and validation symbols are present.
A repeated build reports `ninja: no work to do`.

The finished ZIP is also applied to a separate baseline copy for an incremental
configure/build validation; no source is taken from an unrecorded local overlay.

The available compiler is not the developer's Windows Clang 22.1.8. No claim of
that exact compiler or MSYS2 execution is made.

## Native tests

The test-only System V adapter switches actual Linux x86-64 stacks. The C
scheduler and message-channel implementation are the production sources, not a
model of their state machine. This checks continuation lifetimes, locals,
round-robin selection, wait/wake, sleeping, cancellation, reaping and queues.

| Configuration | Result |
|---|---|
| Clang 17 Debug with UndefinedBehaviorSanitizer | 55/55 passed |
| Clang 17 Release with UndefinedBehaviorSanitizer | 55/55 passed |
| GCC 14 Debug with UndefinedBehaviorSanitizer | 55/55 passed |
| Existing message-channel suite with Clang ASan/UBSan | 72/72 passed |

The 55 cases include 2,000 successive create/run/reap lifetimes. Invalid/stale
handles, slot retirement, full capacity, lost-output prevention, bad stack
margins, corrupted metadata, nested dispatch, counter exhaustion, clock reversal,
maximum deadlines and cancellation restrictions are exercised.

One of the cases runs the exact guest C acceptance sequence with real host
stacks and a host register probe. Its time source, machine CSRs, physical-frame
accounting and original ECALL are explicitly synthetic. That host model does
not prove the RISC-V admission helper or the emulator's timer.

No AddressSanitizer pass is claimed for the alternate-stack suite. Its
hand-written host switch does not implement ASan's fibre notification protocol.

## Guest acceptance added to the image

The new QEMU acceptance path contains six groups:

1. Three threads seed distinct RV64 callee-saved registers, then preserve nested
   stack locals across round-robin yields. Each callback resumes rather than
   restarting from its entry.
2. A sleeping continuation resumes after the dispatcher observes the existing
   machine timer. The timer compare register is not reprogrammed.
3. An explicit waiter resumes after Wake; its handle is refused after Reap.
4. Bounded capacity refuses admission without publishing an output. Ready
   callbacks are cancelled and explicitly collected.
5. A yielding endless callback runs for exactly eight dispatches, then is
   cancelled while suspended. This is not non-cooperative pre-emption.
6. Two callbacks exchange twenty-four copied messages through bounded queues,
   waiting and waking when empty/full, and then reclaim endpoints and stacks.

Final checks compare machine control fields and the timer compare value, invoke
the original ECALL handler, verify all thread records are reaped, and compare
physical-memory counters against the preceding baseline.

## CTest fixture negative test

An intentional compiler error was added only to a separate validation copy.
CTest was asked to run the new cooperative-thread test. The current-image
fixture failed and the dependent runtime test was reported Not Run. This is the
expected refusal of stale-binary evidence.

A non-emulating placeholder path was used solely to register the test while
checking this dependency failure. It was not executed and was not counted as a
successful runtime test.

## Runtime boundary

RISC-V QEMU is unavailable in the local validation environment. **The new
RISC-V cooperative-thread guest has not been executed locally.** Compile,
native alternate-stack tests and the host acceptance model are not substitutes
for the developer's actual QEMU run.

The Windows configure/build/test/run commands and expected markers are in
`COOPERATIVE_THREADS.md`. The normal suite contains twelve tests. Commit only
after that build and the real guest run pass.
