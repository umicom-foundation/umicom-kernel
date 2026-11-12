# Reading a boot handoff safely

A boot handoff is a small description passed from startup code to the kernel. It describes memory and optional display or firmware data. The reader in `kernel/boot_handoff.c` validates a byte buffer and returns an owned observation. It does not boot a machine, map physical memory, initialise a display or replace the existing RISC-V device-tree startup.

## Before starting

Use a Kernel checkout with `include/umicom/kernel/boot_handoff.h`, a C23-capable compiler, CMake 3.24 or later and Ninja. On Windows, the commands below use the MSYS2 UCRT64 compiler. On Linux, use a compiler that supports C23. A host test is a program running inside the current operating system; a guest test runs the kernel inside an emulator. This guide begins with host tests and needs no administrator access or disk image.

## Build the isolated host checks

Run each command separately. Stop if a command fails.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S tests/boot_handoff -B build/host-boot-handoff -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DBUILD_TESTING=ON
cmake --build build/host-boot-handoff --parallel 2
ctest --test-dir build/host-boot-handoff --output-on-failure --no-tests=error
```

```bash
cd ~/umicom/umicom-kernel
cmake -S tests/boot_handoff -B build/host-boot-handoff -G Ninja -DCMAKE_C_COMPILER=gcc -DBUILD_TESTING=ON
cmake --build build/host-boot-handoff --parallel 2
ctest --test-dir build/host-boot-handoff --output-on-failure --no-tests=error
```

<!-- The earlier reader-only expectation is retained for review: all 18 kernel.boot_handoff.* cases pass. The group now includes 28 producer cases. -->
Expected result: all 46 `kernel.boot_handoff.*` cases pass. This is an acceptance criterion, not a statement that these commands have already run. A failed assertion prints the source line and condition. Fix the first failure and rerun its named case before rerunning the group.

## Buffer contract

The caller supplies accessible input bytes and writable output storage. They must not overlap or change during the call. A C pointer alone cannot prove accessibility. The adapter must establish that property before calling the reader. Input size is 64 through 4096 bytes. Exact length is required: trailing bytes and truncated records are refused. The output is unchanged on every refusal. After success, the output owns its arrays and command text and does not borrow input bytes. Physical addresses remain 64-bit integers; the reader never follows them.

## Version 1 wire format

All integers are unsigned and little endian: the least significant byte comes first. No C structure is copied from the wire; byte-wise reads avoid alignment and structure-padding assumptions.

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | `UMIBOOT` followed by a zero byte |
| 8 | 2 | Version, 1 |
| 10 | 2 | Header size, 64 |
| 12 | 4 | Exact total record size |
| 16 | 4 | Architecture: RISC-V 64 = 1, x86-64 = 2, AArch64 = 3 |
| 20 | 4 | Flags, zero |
| 24 | 8 | Bootstrap processor identifier |
| 32 | 8 | Kernel physical start address |
| 40 | 8 | Kernel byte length |
| 48 | 2 | RAM range count, 1 to 32 |
| 50 | 2 | Reserved range count, 0 to 64 |
| 52 | 4 | Firmware table: absent = 0, FDT = 1, ACPI RSDP = 2 |
| 56 | 2 | Command text length, 0 to 255 |
| 58 | 2 | Pixels: absent = 0, RGBX32 = 1, BGRX32 = 2 |
| 60 | 4 | Reserved field, zero |

The sections follow in one order: RAM spans, reserved spans, one firmware span, one framebuffer descriptor, then command text. A span is an eight-byte address followed by an eight-byte length. Even absent optional sections occupy their space and contain zeroes. The framebuffer descriptor is address (8), storage length (8), width (4), height (4), stride in bytes (4), reserved zero (4). Command text has no terminating zero on the wire. The output adds termination in its cleared storage. The length equation is `64 + 16 * (ramCount + reservedCount) + 16 + 32 + commandBytes`.

## Admission rules

1. Check pointers, input length and input/output overlap before reading the header.
2. Check magic, version, flags and recognised field values. Architecture tags are descriptive; they do not provide executable support for those machines.
3. Bound counts before multiplication or array indexing.
4. Require nonempty spans whose exclusive end does not overflow 64 bits. RAM starts and lengths are multiples of 4096, and RAM spans must not overlap. Adjacent RAM spans are allowed.
5. Require the complete kernel span to fit inside one RAM range and one reserved range. Coverage by a union of several entries is deliberately not inferred. Reserved ranges may overlap.
6. For a firmware table, check the descriptor alignment and minimum size. If it intersects RAM, one reservation must cover its whole span. The table contents and checksums require a later format-specific validator.
7. For a framebuffer, check four-byte pixel alignment, nonzero dimensions, sufficient stride and storage. Widen operands before multiplication. Reserve its full span if it intersects RAM. This does not prove a physical mapping is safe to access.
8. Accept only printable ASCII command text. Reject embedded zeroes and control characters. Command interpretation remains a separate concern.
9. Publish the staged observation only after every check succeeds.

## Follow the implementation

`Read16`, `Read32` and `Read64` assemble integers from bytes. `ValidSpan` rejects empty and overflowing intervals. `Contains` compares a remaining length using subtraction. `Intersects` is used only after both spans are valid. `Covered` checks a single covering entry. `ReservedIfRam` connects optional descriptors to reservations. `UmicomKernelBootHandoffRead` orders these checks and publishes once. The existing `UmicomBootMemoryClear` and `UmicomBootMemoryCopy` avoid reliance on a hosted C library.

## Freestanding link qualification

After configuring the existing `riscv64-clang-debug` preset, build `umicom-boot-memory-contracts`. The debug and optimised link images include the real reader and writer, retain uncalled code, and link without the hosted C runtime. They are link checks, not bootable images. A reference to `memcpy` or `memset` in this step is a failure to investigate, not a reason to remove the check.

## Extension boundary

The next integration step is a startup adapter that creates this record from validated platform data, reserves the record and its referenced memory, and passes the observation into existing memory admission. That adapter is not present here. Continue using the existing RISC-V boot path. ARM and x86 boot entries, UEFI handling, ACPI parsing and hardware framebuffer ownership require their own implementation and qualification.

## Produce a validated record

`UmicomKernelBootHandoffWrite` is the producer for this same version-1 format. It accepts a typed `UmicomKernelBootHandoff`, caller-owned destination bytes, destination capacity and a separate written-length counter. The complete input object, the full destination capacity and the counter must be accessible, stable and pairwise nonoverlapping. The input and counter need their declared C alignment. These requirements are the adapter's responsibility; integer checks cannot establish memory accessibility.

~~~c
unsigned char bytes[UMICOM_BOOT_HANDOFF_MAX_BYTES];
UmicomSize written = 0;
UmicomBootHandoffStatus status =
    UmicomKernelBootHandoffWrite(&record, bytes, sizeof bytes, &written);
if (status == UMICOM_BOOT_HANDOFF_OK) {
    /* bytes[0..written) is the exact record; the remaining tail is untouched. */
}
~~~

Here `record` must already describe validated platform observations. A zero-initialised record alone is refused because it has no RAM span or covered kernel. Study the complete synthetic example in Bits2Banking's `labs/native/boot-handoff/write.c`; it runs on the host and never dereferences a physical address.

The writer bounds array counts and command text before encoding. It checks complete enum values before converting to narrower wire fields, so an unknown value cannot truncate into a recognised pixel format. Unused array slots and command bytes after the first terminating zero are not serialized. It then constructs little-endian fields in private storage and calls the existing reader for all range, reservation, firmware and framebuffer rules. Only successful admission allows a final byte copy and length update. Either refusal leaves both caller outputs unchanged.

The largest current record is 1903 bytes, while the API permits destination capacity from 1 through 4096. Larger capacities are deliberately refused rather than silently clamped. The writer uses bounded stack storage and calls the reader, which also stages an observation. A future startup adapter must budget this nested stack use before calling it; no heap allocation or physical reservation occurs here.

### Review independent producer checks

The additional 28 `kernel.boot_handoff.write.*` cases cover literal golden bytes, architectures, optional fields, maximum counts, unused input storage, unaligned byte destinations, untouched tails, insufficient capacity, malformed text, enum narrowing, semantic refusals and overlapping storage. Literal byte expectations provide an independent reference: a writer and reader agreeing on the same mistaken field offset must not be accepted as sufficient evidence.

Run the isolated host commands above to include both reader and writer cases. The `umicom-boot-memory-contracts` link targets include the writer at both debug and optimised settings. A hosted test can pass while a compiler inserts a forbidden `memcpy` call into freestanding code, so both forms of qualification are needed. Neither these authored checks nor a successful host round trip establishes a new firmware entry, x86 boot path or usable bootable ISO.
