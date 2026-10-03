# Umicom Kernel build integrity and stale-image protection

## Why this safeguard exists

A successful CMake configure step does not mean the following compilation and
link succeeded.

When a build fails, the previous `umicom-kernel.elf` can remain in the build
directory.  Running CTest immediately afterwards may then start that older ELF.
The source tree expects the newest readiness markers, while the stale executable
prints the older output.  That creates a confusing set of runtime failures even
though the new runtime image was never produced.

The correct engineering rule is simple:

1. configure;
2. build the current source successfully;
3. only then treat QEMU output as runtime evidence.

## Manual workflow

The normal developer workflow remains incremental:

```powershell
cmake --preset riscv64-clang-debug
```

```powershell
cmake --build `
    --preset riscv64-clang-debug `
    --parallel 2
```

```powershell
ctest `
    --preset riscv64-clang-debug `
    --output-on-failure
```

If the build command fails, stop there and correct the compile/link error.  Do
not interpret QEMU output from an older executable as evidence about the new
source.

## Automatic CTest protection

The CMake configuration now adds one test named:

```text
kernel.build.current
```

That test performs an ordinary incremental CMake/Ninja build of the
`umicom-kernel` target.  It is registered as a CTest fixture setup, and every
QEMU capability test requires that fixture.

This means a direct `ctest` invocation also verifies that the executable is
current before runtime validation begins.

The fixture does not delete the build directory and does not force a clean
rebuild.  Ninja continues to rebuild only the objects affected by source
changes.

## The platform header fix

`include/umicom/kernel/platform.h` contains a disabled historical block wrapped
in `#if 0`.  That block and its existing comments remain preserved.

The previous file used the header guard's final `#endif` to close the disabled
historical block, which left the outer `#ifndef UMICOM_KERNEL_PLATFORM_H`
unterminated.  The correction adds a separate closing directive for the
historical block, followed by the original header-guard close.

Conceptually the end of the file is now:

```c
#if 0

/* Historical source retained for review. */

#endif /* HISTORICAL SHORT PLATFORM NAMES */

#endif /* UMICOM_KERNEL_PLATFORM_H */
```

No active platform function, data structure or device behaviour is removed by
that correction.
