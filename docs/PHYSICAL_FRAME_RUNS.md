# Aligned physical frame runs

![Umicom](../assets/branding/umicom-logo.svg)

The Kernel allocator now accepts a count of consecutive pages and an alignment for the first physical address. It uses the same reserved and allocated bitmaps as the single-page API. This guide describes authored source and expected qualification results; compiler, host, freestanding and platform results must be obtained separately.

## 1. Prepare the isolated checks

Use a C23-capable GCC or Clang, CMake 3.24 or newer and Ninja. These hosted checks treat physical addresses as numbers and never access devices or RAM at those addresses. No administrator rights are needed.

Run each command separately and stop on an error.

~~~powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S tests/physical_memory -B build/host-physical-frames -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DBUILD_TESTING=ON
cmake --build build/host-physical-frames --parallel 2
ctest --test-dir build/host-physical-frames --output-on-failure --no-tests=error
~~~

~~~bash
cd ~/umicom/umicom-kernel
cmake -S tests/physical_memory -B build/host-physical-frames -G Ninja -DCMAKE_C_COMPILER=gcc -DBUILD_TESTING=ON
cmake --build build/host-physical-frames --parallel 2
ctest --test-dir build/host-physical-frames --output-on-failure --no-tests=error
~~~

Expected acceptance: the 40 registered cases pass. This is not an executed result. If a compiler reports a missing declaration, check that both the header and source include the new run interfaces. If configuration used a different compiler previously, choose a new build directory rather than mixing compiler caches.

## 2. Request a run

After the existing boot initialization and reservations:

~~~c
UmicomAddress first = 0;
UmicomKernelMemoryStatus status =
    UmicomKernelPhysicalMemoryAllocateFrames(3U, 4U, &first);
if (status == UMICOM_KERNEL_MEMORY_OK) {
    /* Own three consecutive frames. Establish mappings before access. */
    /* Release after every CPU and device consumer has stopped using them. */
    status = UmicomKernelPhysicalMemoryFreeFrames(first, 3U);
}
~~~

Both parameters count frames, not bytes. The fixed page size is 4096 bytes. Here the extent is 12288 bytes and the first address is aligned to 16384 bytes. Alignment must be a nonzero power of two; the count can be any nonzero size that fits an available run.

Alignment uses absolute physical page numbers. For RAM starting at 0x1000, the first three-page run with alignment four begins at 0x4000. Aligning an index relative to RAM would incorrectly accept 0x1000.

A successful return can publish address zero if zero belongs to the managed RAM range. Use status to distinguish success. The allocator never dereferences the managed addresses or clears their contents.

## 3. Understand refusal

| Request | Result | Ownership |
|---|---|---|
| Null output or zero count | INVALID_ARGUMENT | Unchanged |
| Zero or non-power-of-two alignment | INVALID_ALIGNMENT | Unchanged |
| Valid request before initialization | NOT_INITIALISED | Unchanged |
| Insufficient total free pages | OUT_OF_MEMORY | Unchanged |
| Enough free pages but no aligned contiguous run | OUT_OF_MEMORY | Unchanged |
| Complete eligible run | OK and lowest first address | Every requested page allocated |

The destination remains unchanged on allocation failure. A large valid alignment is checked without multiplying it by page size. A free-page counter alone cannot explain fragmentation; inspect the arrangement of reserved and allocated pages as well.

## 4. Release complete ownership

FreeFrames requires an aligned managed starting address and a nonzero count. It bounds the count by subtracting the starting index from the managed frame count, then checks every frame before mutation. A reserved frame yields RESERVED_FRAME; a missing allocated bit yields NOT_ALLOCATED. Earlier pages remain owned after either refusal.

Allocation groups and owner identities are not recorded. Releasing a subrange is allowed, but the caller must own every released page and must update its remaining ownership record. These bitmaps cannot detect another caller's allocated frame or a stale address that was freed and reused. All accesses to this global allocator require caller serialization; SMP locking is not provided here.

The compatibility AllocateFrame and FreeFrame entry points delegate with a count of one. Their prior implementations remain disabled in the source for engineering review.

## 5. Review independent evidence

The cases assert known addresses, per-frame classifications and field-wise snapshots. They include nonzero RAM origins, fragmentation, final-page barriers, partial release, old/new API coexistence, maximum capacity and top-of-address-space geometry. Comparing only counters could miss a change to the wrong page.

With the existing prepared RISC-V Kernel build, build the **umicom-boot-memory-contracts** target at its documented build directory. The allocator is included in the debug and optimized no-runtime link checks. These ELF files are link-only artifacts; do not boot them. A hosted runtime can conceal an unwanted compiler-generated dependency, which is why hosted results alone are insufficient.

Continue with Bits2Banking's **labs/native/physical-frame-runs** and course lessons 90–93. The hosted executable receives canonical Umicom SVG and ICO resources through Framework. The Kernel core receives no Framework runtime or GUI dependency.

## 6. Keep platform limits visible

This implementation manages one bounded RAM range, at most 65536 frames. It does not supply multiple banks, NUMA, cache maintenance, DMA address translation, an IOMMU, mapping, memory zeroing, hardware discovery or compaction. A driver must retain ownership until hardware completion and meet its platform's addressing and synchronization rules.

A successful allocator test does not qualify an ISO, loader or USB transfer. Keep Umicom OS image and boot-media evidence separate. Record the exact source and environment for each build, test and platform exercise.
