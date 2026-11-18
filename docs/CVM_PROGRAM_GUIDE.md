![Umicom](../assets/branding/umicom-logo.svg)

# Run the bounded CVM inside a native user program

## Current envelope and qualification scope

<!-- Earlier current-envelope summary retained for comparison; selectors, masks and test registration have since advanced.
The diagnostic currently runs selectors **0–51** at **O0 and O2**, producing **104 case markers** before UMICOM_KERNEL_CVM_READY. The host entry suite contains **54 tests**: the 52 selectors plus two unknown-selector boundaries. The source-identity suite contains **17 checks**. Earlier sections below describe how individual profiles were introduced; their smaller historical counts do not describe the current suite. A successful configured test registration is not an executed result.

The optional Windows serial-console fixture now requests selectors **0–51**, the unknown-selector boundaries **52** and **UINT64_MAX**, then repeats selector **0**. That is **55 CVM launches** through the normal console. These are separate from the 104 diagnostic-image markers and require their own execution evidence.

The current runtime feature mask is **82331** and supported wire-version mask is **126**. Query the linked runtime and retain its source receipt. The guest does not link the compiler, so source-only complement bit 32768 must remain absent even though arithmetic bytecode can represent complement.

-->

The diagnostic currently runs selectors **0–72** at **O0 and O2**, producing **146 case markers** before UMICOM_KERNEL_CVM_READY. The host entry suite contains **75 tests**: the 73 selectors plus two unknown-selector boundaries. The source-identity suite contains **31 checks**, including changes and missing files for the bounds API, implementation and guest fixture. Earlier sections describe the introduction of smaller profiles and do not state current coverage. Registration alone is not an executed pass.

The optional Windows serial-console fixture requests selectors **0–72**, unknown selectors **73** and **UINT64_MAX**, then repeats selector **0**. These **76 launches** use the normal console and require their own execution evidence, separate from the diagnostic image.

The current runtime feature mask is **8470939** and supported wire-version mask is **254**. Query the linked runtime and retain its source receipt. The guest does not link the compiler. Source-only grammar features remain absent from the runtime mask; structural path analysis changes neither mask nor the wire format.

Umicom C's interpreter can be linked into a RISC-V ELF user program. ELF is the file format that describes machine code, read-only data and writable storage. The Kernel loads that native program into its own user address space. The program then interprets arithmetic bytecode as data.

This is a native build of an interpreter. It does not compile arbitrary C source directly to RISC-V instructions.

## Source path

1. [programs/cvm/main.c](../programs/cvm/main.c) serializes and decodes four instructions, checks operation identity, and executes them.
2. [programs/cvm/entry.S](../programs/cvm/entry.S) receives the Kernel argument and returns the result through the native EXIT call.
3. [kernel/cvm_validation.c](../kernel/cvm_validation.c) uses the existing process loader, observes U-mode exit, refuses terminal reentry and checks that destroyed processes return their frames.
4. [cmake/CvmPrograms.cmake](../cmake/CvmPrograms.cmake) adds this optional profile. Existing boot profiles remain available.

The source checkout is selected explicitly using `UMICOM_C_SOURCE_DIR`. No compiler repository is fetched automatically. Both O0 and O2 variants link without libc or dead-section elimination. This exposes accidental hosted-library dependencies.

## Expected cases

| Argument | Expected exit value | Purpose |
| --- | --- | --- |
| 0 | 42 | Successful arithmetic and bytecode round trip |
| 1 | 0xc001 | Budget exhausted without overwriting the result |
| 2 | 0xc002 | Division by zero rejected |
| 3 | 0xc003 | Reserved wire flag rejected before execution |
| 4 | 0xc004 | Unknown selector refused |
| 5 | 0xc005 | Complete caller-owned execution trace |
| 6 | 0xc006 | Trace retains the rejected RETURN at fuel exhaustion |
| 7 | 0xc007 | Failed divide retains its two-input stack |
| 8 | 0xc008 | Insufficient trace storage executes nothing |

The diagnostic guest repeats the cases for both optimization modes. Only after every result and teardown check succeeds does it print `UMICOM_KERNEL_CVM_READY`.

## Qualification boundary

Enable `UMICOM_KERNEL_CVM=ON` with the existing RISC-V Clang preset and provide the Umicom C source path. Build the Kernel before selecting `kernel.riscv64.cvm` in CTest. The test uses the current-image fixture and checks a receipt covering external runtime source, build recipe and both ELF images before QEMU runs.

The independent `programs/cvm` CMake project can also build just the two native ELF programs with the same Kernel toolchain. A successful ELF link alone is not a successful guest execution.

This optional diagnostic does not add a compiler command to the normal console or make CVM a required boot service. Firmware entry, normal startup, recovery, filesystems and device tests still require their own qualification. Physical hardware support is not inferred from QEMU.

## Configure and qualify the optional profile

Use a separate build directory so that the ordinary preset retains its existing configuration. These are commands for a prepared toolchain; stop at the first error.

```powershell
Set-Location "C:/umicom/umicom-kernel"
$env:Path = "C:/msys64/ucrt64/bin;$env:Path"
cmake --preset riscv64-clang-debug -B build/riscv64-cvm-debug -DUMICOM_KERNEL_CVM=ON -DUMICOM_C_SOURCE_DIR=C:/umicom/umicom-c
cmake --build build/riscv64-cvm-debug --parallel 2
ctest --test-dir build/riscv64-cvm-debug -R "^kernel[.]riscv64[.]cvm$" --output-on-failure --no-tests=error
```

The required current-image fixture may perform its own build check. If CTest reports no matching test, confirm QEMU was discovered and the optional CVM profile was enabled. Keep the configure, build and test output in your private qualification logs. No test result is claimed by this guide.

## Use the program from the normal console

When the optional CVM profile is enabled, the normal system and interactive console copy the reviewed O2 native envelope into RAMFS as `/bin/umicom-cvm.elf`. All four existing demonstration programs remain available. A build without the option keeps its original program list.

This uses the ordinary `run` command, ELF loader, process supervisor and user-memory boundary. It does not evaluate bytecode inside the privileged console. The example contains fixed arithmetic bytecode; it does not load arbitrary source or bytecode files.

1. Configure and build the separate CVM directory using the commands above.
2. Start the normal image:

   ```powershell
   & "C:/msys64/ucrt64/bin/qemu-system-riscv64.exe" -machine "virt,aclint=off" -bios ".\build\riscv64-cvm-debug\bin\umicom-system.elf" -display none -monitor none -chardev "stdio,id=console,signal=off" -serial "chardev:console" -m 128M -smp 1 -no-reboot
   ```

3. Wait for `umicom> `. Enter `ls /bin` and find `umicom-cvm.elf`.
4. Enter `run /bin/umicom-cvm.elf 0`. The completion report should contain `state=exited exit=42` and `calls=1`. The process identity and slice count vary.
5. Run selectors 1 through 4 from the table above. Console exit values are decimal: 49153, 49154, 49155 and 49156. These values mean the example detected the expected refusal; they do not mean the Kernel crashed.
6. Run selector 0 again to check that an earlier refusal did not prevent another launch.
7. Enter `poweroff` to close the filesystem and leave QEMU.

A native envelope is the small machine-code program that contains the interpreter. The envelope makes the EXIT system call. Its bytecode cannot call Kernel services. Each `run` gets independent process-owned memory.

### Admission and recovery

The console accepts at most five seed programs. Each file remains limited to 128 KiB, and RAMFS retains its total 128-page data budget. A fifth slot does not promise that five maximum-sized files fit. If copying any seed fails, startup closes its partial owner and follows the existing failure/recovery path. It does not discard another tool to make room. The independent recovery console does not launch CVM.

The checked source is in [console_runtime.c](../kernel/console_runtime.c), [console_shell.c](../kernel/console_shell.c) and [console_shell.h](../include/umicom/kernel/console_shell.h).

### Qualify the console route

The host model checks seed bounds, invalid spans, duplicate names, allocation rollback and a fifth program launch. It does not execute RISC-V instructions.

```powershell
cmake -S tests/console_shell -B build/host-console-seeds -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build/host-console-seeds --parallel 2
ctest --test-dir build/host-console-seeds -R "^kernel[.]console[.]seed-" --output-on-failure --no-tests=error
```

On Windows, the serial fixture drives the freshly built normal image through a loopback connection. This avoids relying on piped standard-input behaviour. It retains the original file and diagnostic-program journey, then runs all CVM selectors and repeats the successful case.

```powershell
cmake -S tests/console_session -B build/host-cvm-session -G Ninja -DCMAKE_C_COMPILER=gcc -DUMICOM_CONSOLE_CVM_TESTS=ON -DUMICOM_CONSOLE_IMAGE=C:/umicom/umicom-kernel/build/riscv64-cvm-debug/bin/umicom-console.elf -DUMICOM_CONSOLE_SYSTEM_IMAGE=C:/umicom/umicom-kernel/build/riscv64-cvm-debug/bin/umicom-system.elf
cmake --build build/host-cvm-session --parallel 2
ctest --test-dir build/host-cvm-session -R "^kernel[.]console_session[.]cvm-program$" --output-on-failure --no-tests=error
```

Build the CVM profile immediately before this session. A standalone session test does not rebuild the supplied ELF or establish its source identity. Run the earlier `kernel.riscv64.cvm` receipt check as well. No pass is implied by these instructions.

If the program is missing, check the option and the exact ELF path used by QEMU. If the serial session times out, keep its transcript and report the last command. Do not treat a timeout or an empty test selection as a pass. This profile currently targets the RISC-V QEMU machine described above; it is not a claim of boot support on arbitrary PCs or phones.

## Trace qualification

Selectors 5 through 8 use the same Umicom C executor as ordinary execution. The user program owns four trace entries and checks both successful and rejected states before its single EXIT call. These selectors are separate from 0 through 4; existing results are preserved. In the normal console, expected decimal exits are 49157, 49158, 49159 and 49160. Run selector 0 again afterward to check continued launchability.

The diagnostic guest now checks nine selectors at each optimization setting. The receipt includes `include/umicom_c/trace.h`, and the host identity fixtures reject an edited or missing trace header. Rebuild both native programs after changing that header or the runtime. Reusing an old image because it still prints 42 does not qualify the new trace contract.

The console-session fixture includes all four trace selectors. This is a compact arithmetic observation inside a user program, not an interactive Kernel debugger or a syscall facility for bytecode. Host runtime tests, source receipts, guest execution and normal console interaction remain distinct evidence.

## Comparison profile and host entry checks

Selectors 9–12 extend the existing diagnostic without changing selectors 0–8. They check minimum-versus-maximum signed comparison (true), the reversed comparison (false), a format-2 comparison deliberately mislabelled as format 1, and an execution budget that stops immediately before the comparison. Expected results are `0xc009`, `0xc00a`, `0xc00b` and `0xc00c`. A false comparison is a completed calculation, not an execution failure.

The interpreter remains the canonical Umicom C runtime. Its comparison records use format 2; old arithmetic records retain format 1. Build from matching source and headers with `UMC_COMPARISON_PROFILE`. A stale arithmetic-only runtime is not sufficient for the extended diagnostic. No source compiler, host allocation or system call is introduced into the guest interpreter.

Before guest qualification, you can exercise the real `programs/cvm/main.c` entry function on the host. From a terminal with a C23 compiler, CMake and Ninja on PATH:

```powershell
Set-Location C:\umicom\umicom-kernel
cmake -S tests/cvm_runtime -B build/cvm-envelope-host -G Ninja -DUMICOM_C_SOURCE_DIR=C:/umicom/umicom-c
cmake --build build/cvm-envelope-host
ctest --test-dir build/cvm-envelope-host --output-on-failure
```

Stop after any failed command. The expected suite contains fifteen entry-contract cases, including both unknown selector boundaries. Its result describes host execution only: it does not load an ELF image, switch privilege modes or inspect Kernel frame ownership. Follow the earlier RISC-V build and guest commands separately. The guest validator runs selectors 0–12 under O0 and O2, checks their exit results, rejects terminal reentry and compares frame snapshots after destruction. Record all thirteen case markers for each optimisation and the final ready marker; a host pass cannot substitute for those observations.

If the host build reports a missing comparison profile, check the selected Umicom C path and revision before rebuilding. If the guest receipt refuses stale source, rebuild its two images and capture the new identity through the existing workflow; do not edit a receipt to make it agree. These commands are qualification instructions, not a claim that the checks have already passed.

<!-- Earlier section title retained: Current remainder envelope -->
## Remainder envelope history

The current diagnostic executes selectors 0 through 37 at both O0 and O2. The earlier sections describe how its individual profiles were introduced; their historical counts are not the current suite size. The host entry suite now contains 40 cases, including two unknown-selector boundaries. Every guest selector must preserve process teardown and frame ownership before the final ready marker is emitted.

Selectors 31–37 are implemented in [remainder_checks.h](../programs/cvm/remainder_checks.h), shared by the real guest entry and host entry tests:

| Selector | Expected exit | Checked behavior |
| --- | --- | --- |
| 31 | 0xc01f | Positive remainder and independent format-5 fixture |
| 32 | 0xc020 | Negative dividend retains a negative remainder |
| 33 | 0xc021 | Zero divisor preserves result and trace operands |
| 34 | 0xc022 | Minimum integer / -1 pair rejected before native remainder |
| 35 | 0xc023 | Remainder record mislabelled as format 4 refused |
| 36 | 0xc024 | Instruction budget stops before remainder |
| 37 | 0xc025 | Nonzero reserved remainder operand refused |

Both linked components must advertise the remainder profile. The guest runtime feature mask is 16795 and its supported-format mask is 62. The source receipt now includes the envelope helper itself; an edited or missing helper invalidates old evidence. Reconfigure and rebuild the two native images after changing either the helper or the selected Umicom C runtime.

Run the host and RISC-V commands above against matching checkouts. For receipt regression checks, select **kernel.cvm.evidence.** in the configured root build. Those host fixtures deliberately edit or remove copied inputs and must reject the stale identity. They do not execute guest instructions. Capture all 38 selector results for each optimization mode and the final ready marker for guest qualification. A successful host remainder test, a configured Framework preview test, or an unchanged older image does not establish those guest results.

## Complement and binary bitwise selectors

Selectors 38–42 qualify complement lowered through existing arithmetic: signed minimum, signed maximum, zero, fuel and canonical legacy wire encoding. Selectors 43–51 use [bitwise_checks.h](../programs/cvm/bitwise_checks.h), whose bytes are independent of the encoder. The entry and host tests share this guest contract.

| Selector | Exit code | Contract |
| --- | --- | --- |
| 43 | 0xc02b | AND of -1 and 42 is 42 |
| 44 | 0xc02c | XOR of -1 and 42 is -43 |
| 45 | 0xc02d | OR of -1 and 42 is -1 |
| 46 | 0xc02e | Fuel stops before the operation without publishing a result |
| 47 | 0xc02f | Format 5 refuses a binary bitwise record |
| 48 | 0xc030 | Nonzero reserved operand is refused |
| 49 | 0xc031 | Runtime advertises bitwise and format 6, but not compiler-only complement |
| 50 | 0xc032 | Minimum signed value XOR -1 yields maximum signed value |
| 51 | 0xc033 | Format 6 on a legacy arithmetic program is noncanonical |

The source receipt includes bitwise_checks.h. Its edited and missing-header fixtures must invalidate stale evidence. Rebuild both images before running kernel.riscv64.cvm; the current-image fixture performs the scoped build when invoked through CTest. Preserve all 104 marker observations and the final ready marker. Normal interactive-console workflows and Framework preview qualification are separate checks and are not established by this diagnostic.

## Check the complete normal-console journey

The serial fixture now covers comparisons, locals, branches, conditional updates,
remainder, complement lowering and binary bitwise operations through the ordinary
run command. For selectors 1 through 51, successful contract checks return decimal
49152 plus the selector. Selector 0 returns 42. Unknown selectors 52 and
18446744073709551615 return 49156, the existing refusal result; selector 4 is
itself the historical refusal case. These application exit values are independent
of the QEMU process exit code.

Every accepted CVM response must report a nonzero process identity, at least one
execution slice and exactly one EXIT call. The fixture compares the exit value in
the parsed process report with the expected value, then emits
UMICOM_KERNEL_CVM_CONSOLE_CASE. The final selector-0 run checks that refusals did
not prevent another launch. Expect 55 such host evidence lines and the final
UMICOM_KERNEL_SERIAL_SESSION_PASS marker, with QEMU itself exiting successfully.

The fixture attaches no guest NIC. Its ephemeral 127.0.0.1 connection transports
serial bytes between the host test and its QEMU child; it is not guest networking.
The separate storage journey uses only its named synthetic image opened read-only.
Rebuild current system and console images before using the earlier qualification
commands. Test registration alone cannot show that this longer journey completed.

Read [the console evidence lesson](CVM_CONSOLE_EVIDENCE.md) to compare host runtime,
diagnostic guest and normal-console evidence without treating one as a substitute
for another.

## Structural instruction bounds in user mode

Selectors 69 through 72 exercise the shared Umicom C path analyzer. The guest links the same runtime source used by native hosts; the Kernel does not implement another analyzer.

| Selector | Expected exit | Check |
| --- | --- | --- |
| 69 | 0xc045 (49221) | Eight-record diamond reports structural paths of four and seven instructions |
| 70 | 0xc046 (49222) | Seven steps permit the true route to return 42 |
| 71 | 0xc047 (49223) | A four-step graph can still fail arithmetic at division |
| 72 | 0xc048 (49224) | Backward/self branch refusal leaves the previous bounds unchanged |

The analysis counts both conditional alternatives without evaluating values. A successful fresh execution falls within the bounds, but a fault may occur before the minimum. Fuel is an interpreter operation count, not time, energy, native instruction count or a memory estimate. Both O0 and O2 guest links exercise the allocation-free runtime without libc. The analyzer uses two fixed 256-entry size_t arrays, so its stack cost must be included in the user-process call-chain budget.

The normal console uses decimal selector arguments, for example `run /bin/umicom-cvm.elf 69`. Exit 49221 means that the example verified the expected bounds; it is not the calculated budget. The ordinary process supervisor still owns address spaces, exit observation and teardown. No arbitrary bytecode loader or privileged execution interface is introduced.
