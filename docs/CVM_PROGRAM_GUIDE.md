# Run the bounded CVM inside a native user program

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
