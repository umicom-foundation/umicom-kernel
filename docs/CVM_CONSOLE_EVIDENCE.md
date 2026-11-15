![Umicom](../assets/branding/umicom-logo.svg)

# Follow one VM program through three execution boundaries

A host interpreter test, a diagnostic Kernel image and a normal console session
answer different questions. This lesson follows the same bounded Umicom C virtual
machine through each boundary so you can recognize what the evidence proves.

## What you will learn

1. Distinguish interpreter results from native process and emulator results.
2. Identify which code is privileged and which runs in a user address space.
3. Exercise a complete serial command journey with observable recovery.
4. Keep source identity, expected markers and actual results together.

## 1. Start with the host entry contract

The CVM envelope links the shared Umicom C interpreter into a small program.
Its selector chooses a reviewed example: arithmetic, refusals, tracing,
comparisons, locals, branches, remainder, complement lowering or binary bitwise
operations. The host entry fixture calls that contract directly.

A host pass can show that an interpreter result and diagnostic state match the
example. It does not show that a RISC-V ELF loaded, that the CPU entered user mode,
or that the Kernel reclaimed process memory.

Read [the CVM program guide](CVM_PROGRAM_GUIDE.md) for the current selector table
and matching runtime profile. Do not infer the current feature set from an older
lesson's historical count.

## 2. Add the diagnostic guest boundary

The RISC-V diagnostic loads the native envelope under O0 and O2. These are compiler
optimization settings. Running both helps reveal code that accidentally depends
on one compiler's treatment of undefined or implementation-dependent behavior.

The diagnostic checks each selector's outcome, terminal-process behavior and
frame ownership after destruction. Its current contract contains 52 selectors at
each setting: 104 case markers followed by UMICOM_KERNEL_CVM_READY.

An ELF file is a container for native machine code and data. The native envelope
interprets bytecode as data inside its user address space. This arrangement is
not direct compilation of arbitrary C source to RISC-V machine instructions.

## 3. Add the normal console boundary

The console journey adds serial transport, command parsing, RAMFS lookup, the
ordinary run command and a prompt after each process completes:

~~~text
host fixture
  -> serial bytes
  -> Kernel console parser
  -> run command
  -> RAMFS /bin/umicom-cvm.elf
  -> native user process
  -> bounded Umicom C interpreter
  -> one native EXIT call
  -> completion report
  -> next console prompt
~~~

The bytecode has no native addresses, host callbacks or system-call instructions.
The native envelope performs the single EXIT call after checking its selected
contract. The console itself does not execute the interpreter in privileged mode.

The fixture uses a Windows loopback socket as a serial transport. The socket
belongs to the host test and its QEMU child; the guest has no network device
attached. The ordinary RAM and CVM journeys attach no disk. The separate storage
journey uses only the repository's synthetic read-only test image.

## 4. Read the three kinds of result correctly

A successful selector 0 calculation returns 42. For selectors 1 through 51, the
example returns decimal 49152 plus its selector after observing the expected
contract. Several of those contracts deliberately reject malformed bytecode or
exhausted execution budgets. A nonzero application exit can therefore be the
expected result of a successful regression check.

Unknown selectors 52 and 18446744073709551615 return 49156. Selector 4 is the
historical refusal example and also returns 49156. The fixture repeats selector
0 after those refusals to show that another program can still launch.

For each CVM launch, the parsed process report must contain:

- A nonzero process identity.
- The exact expected application exit value.
- At least one scheduling slice.
- Exactly one native EXIT call.

The host then records UMICOM_KERNEL_CVM_CONSOLE_CASE. Expect 55 of these records:
52 selected examples, two unknown-selector boundaries and one repeated success.
After poweroff, require UMICOM_KERNEL_SERIAL_SESSION_PASS and a successful QEMU
host-process exit as well. A ready prompt alone is insufficient.

## 5. Configure current images and the serial fixture

Use matching Kernel and Umicom C checkouts with a prepared Windows UCRT64
toolchain. Run each command separately and stop on an error. From the Kernel
checkout, adjust the sibling Umicom C path if your directories differ:

~~~powershell
$env:Path = "C:/msys64/ucrt64/bin;$env:Path"
$CSource = (Resolve-Path ../umicom-c).Path
cmake --preset riscv64-clang-debug -B build/riscv64-cvm-debug -DUMICOM_KERNEL_CVM=ON "-DUMICOM_C_SOURCE_DIR=$CSource"
cmake --build build/riscv64-cvm-debug --parallel 2
ctest --test-dir build/riscv64-cvm-debug -R "^kernel[.]riscv64[.]cvm$" --output-on-failure --no-tests=error
~~~

Next configure the Windows host fixture using the images you just built:

~~~powershell
$ConsoleImage = (Resolve-Path build/riscv64-cvm-debug/bin/umicom-console.elf).Path
$SystemImage = (Resolve-Path build/riscv64-cvm-debug/bin/umicom-system.elf).Path
cmake -S tests/console_session -B build/host-cvm-session -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DUMICOM_CONSOLE_CVM_TESTS=ON "-DUMICOM_CONSOLE_IMAGE=$ConsoleImage" "-DUMICOM_CONSOLE_SYSTEM_IMAGE=$SystemImage" -DUMICOM_CONSOLE_QEMU=C:/msys64/ucrt64/bin/qemu-system-riscv64.exe
cmake --build build/host-cvm-session --parallel 2
ctest --test-dir build/host-cvm-session -R "^kernel[.]console_session[.]" --verbose --no-tests=error
~~~

The three registered serial journeys cover RAM files and the diagnostic client,
synthetic read-only storage, and CVM. Keep their results separate from the host
interpreter suite and diagnostic guest suite. Save logs and source/image hashes
with the environment you used; this guide does not claim a result on your machine.

## 6. Diagnose the first failed boundary

If configuration cannot find an image, check the build directory and target
rather than supplying an older ELF. If CVM is absent from RAMFS, verify the optional
profile was enabled when the normal system image was built.

If QEMU cannot connect to serial, inspect the host fixture's process and socket
diagnostics. The fixture forwards child startup output through an explicit handle
allowlist and records an early exit code; it does not inherit the listener or
unrelated host handles. A transport failure is not proof that command parsing is broken.
A restricted host environment can deny loopback connections even when the guest
has no NIC. Use an authorized test environment that permits the fixture's local
serial channel, and keep the failed attempt as separate evidence. Do not weaken
the guest acceptance checks to hide an environmental refusal.
Do not replace the loopback fixture with piped standard input and assume that
Windows delivers identical serial bytes.

If a command returns the wrong application exit, compare the exact selector and
matching runtime source. If the exit is right but the call count or next prompt
is wrong, investigate the native process and console boundary. If only final
poweroff fails, retain the earlier completed records but report the session as
incomplete.

## Exercises

1. Explain why a passing host bitwise test cannot replace selector 43 through the
   normal console.
2. Identify a selector that deliberately refuses malformed input and distinguish
   its expected application result from a failed emulator process.
3. Explain why the final successful launch matters after two refused selectors.
4. Describe what would remain unqualified even after all three evidence layers
   pass: physical hardware, arbitrary source loading and a graphical IDE are
   examples of separate scopes.

Continue with [native program contracts](NATIVE_PROGRAM_CONTRACTS.md) and
[the console learning guide](CONSOLE_LEARNING_GUIDE.md).
