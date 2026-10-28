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
