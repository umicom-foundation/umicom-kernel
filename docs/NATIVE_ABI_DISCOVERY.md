# Native ABI discovery

A native Umicom program can ask the running Kernel which interface it provides.
An **application binary interface**, or **ABI**, describes how compiled code
passes information: which registers carry arguments, which numbers select
operations and how results are represented. It is different from a C function
declaration and from the Framework's portable application services.

The discovery operation is read-only. It does not start a process, open a file,
allocate a channel, inspect private data or grant permissions.

## Query the version first

The existing register convention places a syscall number in `a7`, three
arguments in `a0`, `a1` and `a2`, and executes `ecall`. Discovery uses
operation **3**. The first argument chooses a value; the other two must be zero.
The result replaces `a0`. Other integer registers are preserved.

The public constants live in
[`user_abi.h`](../include/umicom/kernel/user_abi.h). The existing native wrapper
can make this call:

```c
const UmicomU64 version = UmicomRiscvUserSystemCall(
    UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_VERSION, 0U, 0U);
if (version != UMICOM_USER_ABI_VERSION) {
    /* This example requires the exact contract it was written against. */
    return 130U;
}
```

This fragment belongs inside the existing native payload, not a Windows console
program. The wrapper executes a RISC-V instruction and requires the native
guest environment. Read [the real payload](../kernel/user_payload.c) alongside
[the monitor](../kernel/user_monitor.c).

A monitor predating discovery returns `UMICOM_USER_RESULT_UNKNOWN_CALL` (1) for
operation 3. Therefore a consumer must query VERSION and establish compatibility
before interpreting any other selector. A successful FEATURES result can also
be 1: guessing from a feature query alone is ambiguous.

Version `0x00010000` means major 1, minor 0. The upper 16 bits hold the major
version and the lower 16 bits hold the minor version. The high 32 bits are zero.
This number describes a native contract; it does not identify a release build.
A future incompatible contract needs a new major version. An additive selector
or feature can use a compatible minor version when previous meanings remain
unchanged. Consumers must document their accepted versions.

## Values and refusal rules

| Selector in a0 | Meaning | Current value |
| --- | --- | --- |
| 0, VERSION | Packed major/minor interface version | 65536, or 0x00010000 |
| 1, FEATURES | Compiled optional service routes | Bit mask described below |
| 2, WORD_BITS | Register word width of this native contract | 64 |
| 3, BYTE_ORDER | Encoding used by current native wire records | 1, little-endian |
| 4, PAGE_BYTES | Base page size used by the user-memory contract | 4096 |
| 5, COPY_LIMIT | Maximum bytes in the existing COPY syscall | 64 |
| 6, CALL_LIMIT | Existing per-session syscall budget | 32 |

An unknown selector or nonzero `a1` or `a2` returns
`UMICOM_USER_ABI_UNAVAILABLE`: all 64 bits set, written as
`0xffffffffffffffff`. It counts as a rejected call and advances past the ECALL.
No user address is followed. This sentinel is specific to discovery; it does
not replace the established status values of other syscalls.

Zero is a valid FEATURES result. Do not interpret every nonzero result as
failure or assume that zero always means success. Interpret a result according
to the selected operation.

The limit query reports the maximum, not the number of calls left. Discovery
itself consumes the ordinary budget, including refused queries. The existing
origin, executable instruction, continuation and timer checks still apply.
If the monitor cannot safely resume the caller, it ends that invocation rather
than manufacturing a successful discovery result.

## Understand service presence and authority

| Bit value | Service route compiled into the image |
| --- | --- |
| 1 | Copied message channels |
| 2 | Blocking IPC |
| 4 | File operations |
| 8 | Standard input/output/error streams |
| 16 | Managed-service readiness and health reports |

A **bit mask** stores several yes/no answers in one number. To ask whether file
operations are compiled, use `features & UMICOM_USER_ABI_FEATURE_FILES`.
A nonzero result says that the route exists. It says nothing about whether this
particular process owns a valid handle, has the required rights, has an active
service binding or can complete an operation at that moment.

The full current RV64 validation image links all five routes and reports 31.
The independent minimal host fixture links none and reports 0. These are two
different compositions of the same dispatcher. Unknown future bits should be
ignored unless the consumer understands their contract.

The three original operations, EXIT (0), IDENTITY (1) and COPY (2), keep their
numbers and meanings. Existing programs can continue without making discovery
calls. Existing copied message and file structures keep their fixed widths and
size assertions. This feature does not introduce a general extensible structure
negotiation protocol.

## Follow a request through the real source

1. Find `UMICOM_USER_OPERATION_ABI_DISCOVERY` in the
   [user payload](../kernel/user_payload.c). Its C calls enter the existing
   Assembly wrapper.
2. The trap entry captures registers on the machine-owned stack.
3. [The dispatcher](../kernel/user_monitor.c) validates the trap origin, call
   budget, instruction address and next instruction.
4. `UmicomKernelUserAbiQuery` selects a scalar result. Optional feature bits
   follow the same compile definitions as the service routes.
5. The dispatcher writes the result to saved `a0` and advances `mepc` by four
   bytes. Assembly restores the user registers.
6. [The guest validator](../kernel/user_validation.c) checks results from two
   independent address spaces. It also checks exit status, exact call/refusal
   counts, unchanged run counters, machine-state restoration and frame cleanup.

The query does not bypass process isolation. A user-reported value alone is
not proof that a syscall worked: the validator independently checks the
Kernel-side observations too.

## Build and qualify

Follow the [Windows setup guide](WINDOWS_BEGINNER_SETUP.md) first. Run one
command at a time and stop if it fails. Keep build and test logs outside the
source checkout.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug -R "^kernel[.]riscv64[.](abi_discovery|user_execution)$" --output-on-failure --no-tests=error
```

The current-image fixture runs before those guest checks. A successful discovery
test reaches `UMICOM_KERNEL_ABI_DISCOVERY_READY`; the cumulative execution test
also reaches `UMICOM_KERNEL_USER_EXECUTION_READY`. A failure marker, timeout,
missing emulator or stale-image refusal must be investigated rather than
counted as a pass.

Host checks run separately and cannot prove hardware privilege transitions:

```powershell
cmake -S ".\tests\user_memory" -B ".\build\user-memory-host" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe
cmake --build ".\build\user-memory-host" --parallel 2
ctest --test-dir ".\build\user-memory-host" -R "^kernel[.]user_boundary[.]abi-" --output-on-failure --no-tests=error
```

Those checks cover fixed values, unknown selectors, each reserved argument,
complete register preservation, budget exhaustion, wrong privilege and an
invalid continuation. They use the actual mapper, allocator and dispatcher
inside a host process. They also verify that user data remains unchanged.

Keep the source and linked-image identity with the result using the
[build identity guide](BUILD_IDENTITY.md). The
[user-memory guide](USER_MEMORY_BOUNDARIES.md) explains the related complete-span
checks. A native Framework adapter remains separate work; discovery alone does
not make Framework applications runnable on the experimental Kernel.

## Native program client

The [native program contract guide](NATIVE_PROGRAM_CONTRACTS.md) follows the separately linked C client and Assembly bridge. The [user guide](USER_GUIDE.md) explains preparation and log capture step by step.

## Inspect the same contract from the console

The trusted development console accepts `abi` with no arguments. It calls
`UmicomKernelUserAbiQuery`, the same pure metadata reader used after user-origin
checks in the trap dispatcher. It does not simulate an environment call and
does not consume a user process's invocation budget.

The declaration lives in `include/umicom/kernel/native_abi.h`. Presentation
uses immutable static metadata so a freestanding debug build does not require
a compiler-generated aggregate copy through hosted `memcpy`.
See [the console exercise](CONSOLE_LEARNING_GUIDE.md) for commands and expected
meaning. User programs continue using ABI_QUERY through their Assembly bridge.
