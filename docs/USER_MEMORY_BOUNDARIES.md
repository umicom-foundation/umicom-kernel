<!--
Umicom Kernel
File: docs/USER_MEMORY_BOUNDARIES.md
PURPOSE: Explain checked user buffers and separate host contracts from guest isolation.
AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
LICENCE: MIT
-->

# Guide to user-memory boundaries

A user program supplies virtual addresses. A virtual address is a name interpreted
through that process's page table; it is not permission for the Kernel to read an
arbitrary physical byte. The Kernel must check the complete requested range before
copying data across this boundary.

## Follow a copy

The current native environment uses 4,096-byte pages. A 16-byte buffer starting
eight bytes before a page boundary needs two mappings:

```text
User virtual bytes:   [ last 8 bytes of page A ][ first 8 bytes of page B ]
                                 |                         |
Page-table lookup:      physical frame 1            physical frame 3
                                 |                         |
Ownership records:      belongs to caller          belongs to caller
```

The physical frames need not be adjacent. Checking only the first byte cannot
establish that the second page exists, belongs to the caller or permits the
requested operation.

Read `include/umicom/kernel/user_memory.h` and `kernel/user_memory.c`. The existing
service checks canonical addresses, overflow, each page's access bits and its
Kernel-owned backing record. User access is required even when machine mode could
dereference the physical frame. Global mappings and writable executable pages
are outside this environment's policy.

## Why preflight comes first

**Preflight** means checking all required conditions before the first output byte
changes. A read refuses an invalid second source page without filling the first
part of the caller's destination. A write refuses a read-only second page without
changing the first page.

`UmicomKernelUserMemoryRead` and `UmicomKernelUserMemoryWrite` currently admit at
most 64 bytes per call. An empty copy is a documented no-op. The Kernel-side
buffer is trusted caller-owned memory; the user pointer is the value that must
be translated and checked.

These guarantees depend on the current single-hart, non-reentrant monitor.
A **hart** is a hardware execution context. The mapping and ownership records
cannot change concurrently during the check and copy. This is not a multiprocessor
page-pinning or locking implementation.

## Build the host contracts

The host project uses the real allocator, page walker, memory checks and dispatcher.
An aligned array provides test physical storage. It does not execute RISC-V
privileged instructions.

Run one command at a time and stop on an error:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S ".\tests\user_memory" -B ".\build\host-user-memory" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe
cmake --build ".\build\host-user-memory" --parallel 2
ctest --test-dir ".\build\host-user-memory" --parallel 2 --output-on-failure --no-tests=error
```

The tests sweep every possible split of a 64-byte copy across two pages. They also
check missing and read-only second pages, missing ownership, inconsistent backing,
a hole between valid endpoints, unchanged refusal output and allocator cleanup.
Each named case starts with fresh process-local storage.

The build enables strict warnings as errors. Sanitizers are an additional,
optional configuration on toolchains that support them; an ordinary host pass
must not be described as sanitizer evidence.

## Check actual guest execution separately

The `kernel.riscv64.user_execution` test boots the guest and exercises actual
privilege transitions, page faults, forbidden supervisor access, invalid stack
handling and a surviving task after another task's faults. Follow the repository's
RISC-V toolchain and QEMU setup first.

```powershell
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug -R "^kernel\.riscv64\.user_execution$" --output-on-failure --no-tests=error
```

A host-contract pass and a guest-execution pass answer different questions. Keep
both results with the tested source identity; see [build identity](BUILD_IDENTITY.md).
A test that never started or timed out is not a successful isolation check.

## Make a useful change

Before adding a syscall that reads or writes a user buffer:

1. Define its maximum byte count and whether zero length is valid.
2. State which side owns each buffer and which access rights it needs.
3. Validate the entire span before publishing output.
4. Add a case where the first page is valid and a later page is invalid.
5. Check complete unchanged destinations on refusal, not just the return code.
6. Qualify the actual guest path as well as the host contract.

Do not dereference an address supplied by user registers as a Kernel pointer.
The syscall wrapper and shared copy service must preserve the same rules.
