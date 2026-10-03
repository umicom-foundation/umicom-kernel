/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/main.c
 *
 * PURPOSE:
 *   Preserve K1/K2 boot-and-trap evidence and extend the executable teaching
 *   path with K3's first physical page-frame allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   K3 adds memory ownership without pretending that virtual memory exists.
 *
 *   The execution sequence is deliberately cumulative:
 *
 *     K1: machine bootstrap -> C23 -> serial console;
 *     K2: synchronous exception + asynchronous timer interrupt;
 *     K3: discover fixed-profile RAM, protect Kernel/DTB pages, allocate/free
 *         physical 4 KiB frames, and prove safety/error cases.
 *
 *   Every earlier milestone marker remains in the output so later code cannot
 *   silently break the original boot/trap path while still claiming K3 success.
 *
 * NAMING NOTE:
 *   New K3 source uses full `Umicom...` names.  Headers retain compatibility
 *   aliases for K1/K2's already committed `Umi...` symbols rather than deleting
 *   historical APIs merely for cosmetic consistency.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import overflow/alignment-safe address helpers used by K3 validation. */
#include "umicom/kernel/address.h"

/* Import static product/milestone identity strings. */
#include "umicom/kernel/build.h"

/* Import the freestanding early serial-formatting layer. */
#include "umicom/kernel/console.h"

/* Import the minimum FDT header inspection needed to protect DTB pages. */
#include "umicom/kernel/device_tree.h"

/* Import the C entry-point declaration shared with boot.S. */
#include "umicom/kernel/kernel.h"

/* Import linker-defined Kernel image boundaries. */
#include "umicom/kernel/linker.h"

/* Import the first physical page-frame allocator. */
#include "umicom/kernel/physical_memory.h"

/* Import platform timer, RAM description and QEMU finish/halt operations. */
#include "umicom/kernel/platform.h"

/* Import RV64 trap installation, test triggers and snapshots. */
#include "umicom/kernel/riscv64/trap.h"

/* QEMU's selected CLINT-compatible timer runs at 10 MHz.
 *
 * 100,000 ticks therefore represents approximately 10 ms. */
#define UMICOM_K2_TIMER_DELTA_TICKS ((UmicomU64)100000U)

/* Write one "key=value" text line without relying on printf/libc. */
static void WriteKeyValue(const char *key, const char *value)
{
    /* Emit the field name. */
    UmicomKernelConsoleWrite(key);

    /* Separate name from value using a predictable machine-readable character. */
    UmicomKernelConsoleWrite("=");

    /* Emit the value followed by the console's standard CR+LF ending. */
    UmicomKernelConsoleWriteLine(value);
}

/* Write one decimal unsigned record. */
static void WriteUnsignedRecord(const char *key, UmicomU64 value)
{
    /* Emit the field name. */
    UmicomKernelConsoleWrite(key);

    /* Emit the key/value separator. */
    UmicomKernelConsoleWrite("=");

    /* Convert the 64-bit integer with the freestanding decimal helper. */
    UmicomKernelConsoleWriteUnsigned((UmiU64)value);

    /* Terminate the record. */
    UmicomKernelConsoleWriteLine("");
}

/* Write one fixed-width hexadecimal record. */
static void WriteHexRecord(const char *key, UmicomU64 value)
{
    /* Emit the field name. */
    UmicomKernelConsoleWrite(key);

    /* Emit the key/value separator. */
    UmicomKernelConsoleWrite("=");

    /* Render the complete 64-bit value with 0x prefix. */
    UmicomKernelConsoleWriteHex64((UmiU64)value);

    /* Terminate the record. */
    UmicomKernelConsoleWriteLine("");
}

/* Write one physical-memory status as stable readable text. */
static void WriteMemoryStatusRecord(
    const char *key,
    UmicomKernelMemoryStatus status
)
{
    /* Convert the enum to a stable diagnostic name owned by the allocator. */
    const char *const statusName = UmicomKernelMemoryStatusName(status);

    /* Publish the resulting text using the same machine-readable record style. */
    WriteKeyValue(key, statusName);
}

/* Stop a failed acceptance path with human-readable evidence.
 *
 * The function intentionally does not return to its caller. */
static void FailKernel(UmicomU32 code, const char *reason)
{
    /* Give CTest and humans an unambiguous failure marker. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");

    /* Emit the exact failure reason without hosted formatting. */
    WriteKeyValue("reason", reason);

    /* Ask QEMU to exit with a nonzero bounded test code. */
    UmicomPlatformFinishFailure((UmiU32)code);

    /* Retain a final non-returning safety boundary if the QEMU device is absent. */
    UmicomPlatformHalt();
}

/* Verify K2's deliberate M-mode ECALL exception remains healthy. */
static void RunExceptionTest(void)
{
    /* Use caller-owned stack storage; K2 itself still needs no allocator. */
    UmicomRiscvTrapSnapshot snapshot;

    /* Announce the operation before deliberately entering the exception path. */
    UmicomKernelConsoleWriteLine("exception-test=begin");

    /* Execute ECALL in M-mode.
     *
     * trap.c recognises cause 11, records it and advances mepc by four bytes. */
    UmicomRiscvTriggerMachineEcall();

    /* Copy the trap handler's stable post-exception evidence. */
    UmicomRiscvTrapSnapshotRead(&snapshot);

    /* Publish the important values before deciding whether the test passed. */
    WriteUnsignedRecord("trap.exception.count", (UmicomU64)snapshot.exceptionCount);
    WriteUnsignedRecord("trap.exception.cause", (UmicomU64)snapshot.lastCauseCode);
    WriteHexRecord("trap.exception.mepc", (UmicomU64)snapshot.lastMepc);
    WriteHexRecord("trap.exception.mtval", (UmicomU64)snapshot.lastMtval);

    /* Exactly one deliberate exception must have been recognised. */
    if (snapshot.exceptionCount != (UmiU64)1U) {
        /* Terminate with a K2-compatible diagnostic reason. */
        FailKernel((UmicomU32)10U, "exception-count");
    }

    /* An ECALL is synchronous, so the interrupt flag must have been clear. */
    if (snapshot.lastWasInterrupt != (UmiU64)0U) {
        /* Refuse to treat an incorrectly classified trap as a pass. */
        FailKernel((UmicomU32)11U, "exception-marked-as-interrupt");
    }

    /* M-mode ECALL is architectural cause code 11. */
    if (snapshot.lastCauseCode != UMICOM_RISCV_EXCEPTION_ECALL_M_MODE) {
        /* The trap vector ran, but it did not report the event we triggered. */
        FailKernel((UmicomU32)12U, "wrong-ecall-cause");
    }

    /* Reaching this marker proves MRET resumed after the ECALL instruction. */
    UmicomKernelConsoleWriteLine("exception-test=pass");
}

/* Verify one real QEMU machine-timer interrupt remains healthy. */
static void RunTimerTest(UmicomU64 hartId)
{
    /* Keep pre/post trap evidence in caller-owned stack storage. */
    UmicomRiscvTrapSnapshot snapshot;

    /* State clearly that the asynchronous test is beginning. */
    UmicomKernelConsoleWriteLine("timer-test=begin");

    /* Read QEMU's current 64-bit machine time. */
    const UmicomU64 now = (UmicomU64)UmicomPlatformTimerRead();

    /* Determine the greatest representable 64-bit unsigned value. */
    const UmicomU64 maximum = ~(UmicomU64)0U;

    /* Refuse integer wrap rather than programming a deadline in the past. */
    if (now > maximum - UMICOM_K2_TIMER_DELTA_TICKS) {
        /* A wrapped timer deadline would invalidate asynchronous evidence. */
        FailKernel((UmicomU32)20U, "timer-deadline-overflow");
    }

    /* Add the deliberate ~10 ms interval after proving the addition is safe. */
    const UmicomU64 deadline = now + UMICOM_K2_TIMER_DELTA_TICKS;

    /* Publish both values so learners can see the absolute timer model. */
    WriteUnsignedRecord("timer.now", now);
    WriteUnsignedRecord("timer.deadline", deadline);

    /* Program mtimecmp before enabling interrupts. */
    UmicomPlatformTimerSetCompare((UmiU64)hartId, (UmiU64)deadline);

    /* Enable mie.MTIE and then mstatus.MIE through the K2 Assembly helper. */
    UmicomRiscvMachineTimerInterruptEnable();

    /* Continue until the handler records the one controlled timer event. */
    for (;;) {
        /* Read current trap state before sleeping, closing the race where the
         * timer may already have fired immediately after interrupt enable. */
        UmicomRiscvTrapSnapshotRead(&snapshot);

        /* Stop waiting once the interrupt handler has completed. */
        if (snapshot.timerInterruptCount != (UmiU64)0U) {
            /* Exit the polling/sleep loop without executing another WFI. */
            break;
        }

        /* Otherwise wait efficiently for an interrupt/event. */
        UmicomRiscvWaitForInterrupt();
    }

    /* Return to K2's deterministic interrupts-disabled state. */
    UmicomRiscvMachineTimerInterruptDisable();

    /* Read one final stable copy after interrupt delivery is disabled. */
    UmicomRiscvTrapSnapshotRead(&snapshot);

    /* Publish the timer evidence. */
    WriteUnsignedRecord("trap.timer.count", (UmicomU64)snapshot.timerInterruptCount);
    WriteUnsignedRecord("trap.timer.cause", (UmicomU64)snapshot.lastCauseCode);
    WriteHexRecord("trap.timer.mepc", (UmicomU64)snapshot.lastMepc);

    /* K2 expects exactly one one-shot timer interrupt. */
    if (snapshot.timerInterruptCount != (UmiU64)1U) {
        /* Multiple or missing interrupts are both invalid evidence. */
        FailKernel((UmicomU32)21U, "timer-count");
    }

    /* The last event must have arrived through the interrupt path. */
    if (snapshot.lastWasInterrupt != (UmiU64)1U) {
        /* Refuse an incorrectly classified timer event. */
        FailKernel((UmicomU32)22U, "timer-marked-as-exception");
    }

    /* Architectural machine-timer interrupt code is 7. */
    if (snapshot.lastCauseCode != UMICOM_RISCV_INTERRUPT_MACHINE_TIMER) {
        /* The interrupt path executed, but not for the expected source. */
        FailKernel((UmicomU32)23U, "wrong-timer-cause");
    }

    /* Confirm that the timer did not corrupt the earlier ECALL evidence count. */
    if (snapshot.exceptionCount != (UmiU64)1U) {
        /* Trap state from the two event classes must remain independently valid. */
        FailKernel((UmicomU32)24U, "exception-count-changed");
    }

    /* Announce successful asynchronous trap delivery/return. */
    UmicomKernelConsoleWriteLine("timer-test=pass");
}

/* Prove K3's physical frame allocator and its failure boundaries. */
static void RunPhysicalMemoryTest(UmicomAddress deviceTreeAddress)
{
    /* Announce the new milestone's test section. */
    UmicomKernelConsoleWriteLine("memory-test=begin");

    /* Ask the QEMU platform adapter for the RAM geometry matched by -m 128M. */
    UmicomPlatformPhysicalMemoryInfo platformMemory;

    /* Fill the caller-owned structure with QEMU profile data. */
    UmicomPlatformPhysicalMemoryDescribe(&platformMemory);

    /* A zero-byte platform result would make every later memory operation invalid. */
    if (platformMemory.bytes == (UmicomSize)0U) {
        /* Treat missing platform memory geometry as a K3 acceptance failure. */
        FailKernel((UmicomU32)30U, "platform-memory-empty");
    }

    /* Publish the physical RAM geometry before allocator initialization. */
    WriteHexRecord("memory.ram.base", (UmicomU64)platformMemory.base);
    WriteUnsignedRecord("memory.ram.bytes", (UmicomU64)platformMemory.bytes);
    WriteUnsignedRecord("memory.page.bytes", (UmicomU64)UMICOM_KERNEL_PAGE_SIZE);

    /* Validate enough of QEMU's FDT header to discover the complete blob size. */
    UmicomKernelDeviceTreeInfo deviceTreeInfo;

    /* Refuse to reserve an unvalidated DTB pointer/size. */
    if (
        UmicomKernelDeviceTreeInspect(
            deviceTreeAddress,
            platformMemory.base,
            platformMemory.bytes,
            &deviceTreeInfo
        ) == UMICOM_FALSE
    ) {
        /* A bad FDT header/range must not be guessed around. */
        FailKernel((UmicomU32)31U, "device-tree-invalid");
    }

    /* Publish the actual validated DTB range supplied by QEMU. */
    WriteHexRecord("memory.dtb.start", (UmicomU64)deviceTreeInfo.address);
    WriteUnsignedRecord("memory.dtb.bytes", (UmicomU64)deviceTreeInfo.totalBytes);

    /* Read linker-defined image boundaries as target addresses. */
    const UmicomAddress kernelStart =
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start);

    /* Read the exclusive end including BSS, bitmaps and bootstrap stack. */
    const UmicomAddress kernelEnd =
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end);

    /* Publish both image boundaries for inspection against llvm-nm/linker map. */
    WriteHexRecord("memory.kernel.start", (UmicomU64)kernelStart);
    WriteHexRecord("memory.kernel.end", (UmicomU64)kernelEnd);

    /* The linked image must live at or above the platform RAM base. */
    if (kernelStart < platformMemory.base) {
        /* A Kernel image outside declared RAM invalidates this K3 profile. */
        FailKernel((UmicomU32)32U, "kernel-start-outside-ram");
    }

    /* The exclusive Kernel end must be strictly above its first byte. */
    if (kernelEnd <= kernelStart) {
        /* Linker boundaries must describe a non-empty ordered image. */
        FailKernel((UmicomU32)33U, "kernel-range-invalid");
    }

    /* Initialise the allocator over the complete QEMU RAM profile. */
    UmicomKernelMemoryStatus status =
        UmicomKernelPhysicalMemoryInitialize(
            platformMemory.base,
            platformMemory.bytes
        );

    /* Publish the initialization result before asserting it. */
    WriteMemoryStatusRecord("memory.initialize", status);

    /* Initialization must succeed for the fixed 128 MiB profile. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* The status line above explains which allocator guard rejected it. */
        FailKernel((UmicomU32)34U, "memory-initialize");
    }

    /* Reserve from RAM base through the complete linked Kernel image.
     *
     * This intentionally protects the 2 MiB low-RAM gap below 0x80200000 as
     * well as the executable/data/BSS/allocator-bitmaps/bootstrap stack.  K3
     * has not yet created a separate early-boot allocator for that low region. */
    const UmicomSize lowKernelBytes =
        (UmicomSize)(kernelEnd - platformMemory.base);

    /* Apply that low-memory/Kernel reservation transactionally. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        platformMemory.base,
        lowKernelBytes
    );

    /* Publish the reservation result. */
    WriteMemoryStatusRecord("memory.reserve.kernel", status);

    /* The Kernel must never continue if its own pages remain allocatable. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Protecting ourselves is a prerequisite for any frame allocation. */
        FailKernel((UmicomU32)35U, "reserve-kernel");
    }

    /* Reserve every page touched by the validated QEMU DTB blob. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        deviceTreeInfo.address,
        deviceTreeInfo.totalBytes
    );

    /* Publish the DTB ownership result. */
    WriteMemoryStatusRecord("memory.reserve.dtb", status);

    /* Device-tree data must remain intact for later hardware discovery work. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* An overlap here would reveal an invalid K3 physical layout. */
        FailKernel((UmicomU32)36U, "reserve-device-tree");
    }

    /* Capture baseline accounting after permanent K3 reservations are installed. */
    UmicomKernelPhysicalMemorySnapshot baseline;

    /* Ask the allocator to copy its current counters. */
    status = UmicomKernelPhysicalMemorySnapshotRead(&baseline);

    /* A snapshot failure means public accounting cannot be trusted. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop before running ownership tests against unknown counters. */
        FailKernel((UmicomU32)37U, "baseline-snapshot");
    }

    /* Publish the initial physical-frame accounting. */
    WriteUnsignedRecord("memory.total.frames", (UmicomU64)baseline.totalFrames);
    WriteUnsignedRecord("memory.reserved.frames", (UmicomU64)baseline.reservedFrames);
    WriteUnsignedRecord("memory.allocated.frames", (UmicomU64)baseline.allocatedFrames);
    WriteUnsignedRecord("memory.free.frames", (UmicomU64)baseline.freeFrames);

    /* The fixed 128 MiB / 4 KiB profile must contain exactly 32,768 frames. */
    if (baseline.totalFrames != (UmicomSize)32768U) {
        /* This catches a mismatch between QEMU command and platform description. */
        FailKernel((UmicomU32)38U, "unexpected-frame-count");
    }

    /* No ordinary frame has been allocated yet. */
    if (baseline.allocatedFrames != (UmicomSize)0U) {
        /* Initialization/reservation must not fabricate caller allocations. */
        FailKernel((UmicomU32)39U, "baseline-allocated-nonzero");
    }

    /* Permanent Kernel+DTB reservations must leave usable RAM available. */
    if (baseline.freeFrames == (UmicomSize)0U) {
        /* There would be no point continuing to allocation tests. */
        FailKernel((UmicomU32)40U, "no-free-frames");
    }

    /* Prove the allocator's bitmap/counter invariants before mutations. */
    status = UmicomKernelPhysicalMemoryValidate();

    /* Publish the independent recount result. */
    WriteMemoryStatusRecord("memory.validate.baseline", status);

    /* Bitmap/counter disagreement is a Kernel bug, not a recoverable allocation error. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop on internal metadata corruption. */
        FailKernel((UmicomU32)41U, "baseline-invariant");
    }

    /* Exercise the shared overflow helper with the maximum possible address. */
    UmicomAddress ignoredOverflowResult = (UmicomAddress)0U;

    /* max-address + 1 must be rejected rather than wrapping to zero. */
    if (
        UmicomKernelAddressAddChecked(
            ~(UmicomAddress)0U,
            (UmicomSize)1U,
            &ignoredOverflowResult
        ) != UMICOM_FALSE
    ) {
        /* Checked arithmetic is a prerequisite for trustworthy range validation. */
        FailKernel((UmicomU32)42U, "address-overflow-accepted");
    }

    /* Announce successful overflow refusal in deterministic evidence. */
    UmicomKernelConsoleWriteLine("memory.address-overflow=refused");

    /* Allocate the first ordinary free physical frame. */
    UmicomAddress frameA = (UmicomAddress)0U;

    /* Ask the allocator for deterministic lowest-addressed free page A. */
    status = UmicomKernelPhysicalMemoryAllocateFrame(&frameA);

    /* Publish the exact result before checking it. */
    WriteMemoryStatusRecord("memory.allocate.a.status", status);

    /* K3 must have at least one allocatable frame. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop with explicit evidence instead of using an invalid address. */
        FailKernel((UmicomU32)43U, "allocate-a");
    }

    /* Publish the first allocated physical page address. */
    WriteHexRecord("memory.allocate.a", (UmicomU64)frameA);

    /* Allocate a second distinct frame to prove ownership advances. */
    UmicomAddress frameB = (UmicomAddress)0U;

    /* Ask for another lowest-address free page. */
    status = UmicomKernelPhysicalMemoryAllocateFrame(&frameB);

    /* Publish the exact result. */
    WriteMemoryStatusRecord("memory.allocate.b.status", status);

    /* A second frame should remain available in a 128 MiB test machine. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Allocation failure here indicates broken accounting/reservation. */
        FailKernel((UmicomU32)44U, "allocate-b");
    }

    /* Publish the second physical page address. */
    WriteHexRecord("memory.allocate.b", (UmicomU64)frameB);

    /* Two successful allocations must never return the same physical frame. */
    if (frameA == frameB) {
        /* Duplicate ownership would allow immediate memory corruption. */
        FailKernel((UmicomU32)45U, "duplicate-allocation");
    }

    /* Both returned frame addresses must obey the 4 KiB alignment contract. */
    if (
        (frameA & (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - 1U)) !=
        (UmicomAddress)0U
    ) {
        /* An unaligned page-frame handle cannot be safely mapped later. */
        FailKernel((UmicomU32)46U, "frame-a-misaligned");
    }

    /* Apply the same alignment proof to frame B. */
    if (
        (frameB & (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - 1U)) !=
        (UmicomAddress)0U
    ) {
        /* Refuse inconsistent allocator output. */
        FailKernel((UmicomU32)47U, "frame-b-misaligned");
    }

    /* Release A so we can exercise reservation semantics on a known free page. */
    status = UmicomKernelPhysicalMemoryFreeFrame(frameA);

    /* The first ordinary free must succeed. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop if ownership could not be relinquished cleanly. */
        FailKernel((UmicomU32)48U, "free-a");
    }

    /* Convert the now-free A page into a one-page explicit reservation. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        frameA,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the reservation operation result. */
    WriteMemoryStatusRecord("memory.reserve.a", status);

    /* A previously freed page should be reservable. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Refuse to continue with a page whose ownership is uncertain. */
        FailKernel((UmicomU32)49U, "reserve-a");
    }

    /* FreeFrame must refuse a page owned by the reserved set. */
    status = UmicomKernelPhysicalMemoryFreeFrame(frameA);

    /* Publish the protection result. */
    WriteMemoryStatusRecord("memory.free.reserved", status);

    /* Reserved-frame protection is a specific safety contract. */
    if (status != UMICOM_KERNEL_MEMORY_RESERVED_FRAME) {
        /* Accepting the free would expose Kernel/platform memory to reuse. */
        FailKernel((UmicomU32)50U, "reserved-free-not-refused");
    }

    /* Reserving the exact same page again must be rejected as overlap. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        frameA,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the overlap test result. */
    WriteMemoryStatusRecord("memory.reserve.overlap", status);

    /* The allocator must not silently make overlapping reservations idempotent. */
    if (status != UMICOM_KERNEL_MEMORY_OVERLAP) {
        /* Explicit overlap detection protects ownership assumptions. */
        FailKernel((UmicomU32)51U, "reservation-overlap-not-refused");
    }

    /* Release the temporary A reservation through the explicit reservation API. */
    status = UmicomKernelPhysicalMemoryReleaseReservedRange(
        frameA,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the release result. */
    WriteMemoryStatusRecord("memory.release.a", status);

    /* The matching reservation/release pair must succeed. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* A leaked reservation would permanently reduce free RAM. */
        FailKernel((UmicomU32)52U, "release-a");
    }

    /* Releasing the same page again exercises double-release refusal. */
    status = UmicomKernelPhysicalMemoryReleaseReservedRange(
        frameA,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the second release result. */
    WriteMemoryStatusRecord("memory.release.again", status);

    /* The page is no longer reserved and must be reported honestly. */
    if (status != UMICOM_KERNEL_MEMORY_NOT_RESERVED) {
        /* Silent repeated release would hide ownership bugs. */
        FailKernel((UmicomU32)53U, "reservation-double-release-not-refused");
    }

    /* Allocate again after releasing A's reservation. */
    UmicomAddress frameC = (UmicomAddress)0U;

    /* Deterministic first-fit should select the lowest free page, which is A. */
    status = UmicomKernelPhysicalMemoryAllocateFrame(&frameC);

    /* Publish the re-allocation result and address. */
    WriteMemoryStatusRecord("memory.allocate.c.status", status);

    /* Allocation must succeed after reservation release. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* This would indicate leaked ownership state. */
        FailKernel((UmicomU32)54U, "allocate-c");
    }

    /* Publish the deterministic reuse address. */
    WriteHexRecord("memory.allocate.c", (UmicomU64)frameC);

    /* Lowest-address-first allocation should reuse A before later free pages. */
    if (frameC != frameA) {
        /* K3 promises deterministic first-fit behaviour for educational tests. */
        FailKernel((UmicomU32)55U, "deterministic-reuse");
    }

    /* Free C normally. */
    status = UmicomKernelPhysicalMemoryFreeFrame(frameC);

    /* The first free of an allocated page must succeed. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop before testing the second free. */
        FailKernel((UmicomU32)56U, "free-c");
    }

    /* Free C a second time to prove double-free detection. */
    status = UmicomKernelPhysicalMemoryFreeFrame(frameC);

    /* Publish the double-free result. */
    WriteMemoryStatusRecord("memory.free.double", status);

    /* An already-free frame must be reported as not allocated. */
    if (status != UMICOM_KERNEL_MEMORY_NOT_ALLOCATED) {
        /* Double free must never silently succeed. */
        FailKernel((UmicomU32)57U, "double-free-not-refused");
    }

    /* Freeing an address inside a frame rather than its page start must fail. */
    status = UmicomKernelPhysicalMemoryFreeFrame(
        frameC + (UmicomAddress)1U
    );

    /* Publish the alignment guard result. */
    WriteMemoryStatusRecord("memory.free.misaligned", status);

    /* The allocator must distinguish malformed frame handles. */
    if (status != UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT) {
        /* Accepting a byte-offset pointer as a frame would corrupt indexing. */
        FailKernel((UmicomU32)58U, "misaligned-free-not-refused");
    }

    /* Free the still-live second allocation. */
    status = UmicomKernelPhysicalMemoryFreeFrame(frameB);

    /* B must return to the free set cleanly. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop if ordinary allocation accounting cannot return to baseline. */
        FailKernel((UmicomU32)59U, "free-b");
    }

    /* Calculate the exclusive RAM end for an out-of-range reservation test. */
    UmicomAddress ramEnd = (UmicomAddress)0U;

    /* This addition already succeeded during initialization but is tested again
     * through the public checked helper used by ordinary Kernel code. */
    if (
        UmicomKernelAddressAddChecked(
            platformMemory.base,
            platformMemory.bytes,
            &ramEnd
        ) == UMICOM_FALSE
    ) {
        /* Internal platform geometry unexpectedly failed checked arithmetic. */
        FailKernel((UmicomU32)60U, "ram-end-overflow");
    }

    /* A new page beginning exactly at exclusive RAM end is outside managed RAM. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        ramEnd,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the range-boundary refusal. */
    WriteMemoryStatusRecord("memory.reserve.outside", status);

    /* The manager must never clip an invalid request into RAM silently. */
    if (status != UMICOM_KERNEL_MEMORY_OUTSIDE_RAM) {
        /* Explicit physical boundaries are part of K3's safety contract. */
        FailKernel((UmicomU32)61U, "outside-reservation-not-refused");
    }

    /* Reserving an already protected Kernel page must also report overlap. */
    status = UmicomKernelPhysicalMemoryReserveRange(
        kernelStart,
        UMICOM_KERNEL_PAGE_SIZE
    );

    /* Publish the Kernel-overlap result. */
    WriteMemoryStatusRecord("memory.reserve.kernel-overlap", status);

    /* Kernel pages must remain protected from a second owner. */
    if (status != UMICOM_KERNEL_MEMORY_OVERLAP) {
        /* Any other result weakens the protected-range model. */
        FailKernel((UmicomU32)62U, "kernel-overlap-not-refused");
    }

    /* Independently recount every active bitmap bit after all mutation tests. */
    status = UmicomKernelPhysicalMemoryValidate();

    /* Publish final invariant validation. */
    WriteMemoryStatusRecord("memory.validate.final", status);

    /* The allocator must end in a self-consistent state. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Counter/bitmap drift is a Kernel defect. */
        FailKernel((UmicomU32)63U, "final-invariant");
    }

    /* Capture final accounting after every temporary allocation/reservation was undone. */
    UmicomKernelPhysicalMemorySnapshot finalSnapshot;

    /* Ask for the final stable counters. */
    status = UmicomKernelPhysicalMemorySnapshotRead(&finalSnapshot);

    /* A failed final snapshot would make baseline comparison impossible. */
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        /* Stop rather than claiming all ownership returned to baseline. */
        FailKernel((UmicomU32)64U, "final-snapshot");
    }

    /* All temporary allocations must have been released. */
    if (finalSnapshot.allocatedFrames != (UmicomSize)0U) {
        /* K3's self-test must not leak physical frames. */
        FailKernel((UmicomU32)65U, "allocation-leak");
    }

    /* Permanent reservation count must match the post-bootstrap baseline. */
    if (finalSnapshot.reservedFrames != baseline.reservedFrames) {
        /* Temporary reservation tests must not leak protected pages. */
        FailKernel((UmicomU32)66U, "reservation-leak");
    }

    /* Free-frame count must return exactly to the baseline value. */
    if (finalSnapshot.freeFrames != baseline.freeFrames) {
        /* This catches ownership loss even when individual counters look plausible. */
        FailKernel((UmicomU32)67U, "free-frame-accounting");
    }

    /* Publish final counters so the successful state is inspectable. */
    WriteUnsignedRecord(
        "memory.final.reserved.frames",
        (UmicomU64)finalSnapshot.reservedFrames
    );
    WriteUnsignedRecord(
        "memory.final.allocated.frames",
        (UmicomU64)finalSnapshot.allocatedFrames
    );
    WriteUnsignedRecord(
        "memory.final.free.frames",
        (UmicomU64)finalSnapshot.freeFrames
    );

    /* Announce completion of all K3 success and refusal-path checks. */
    UmicomKernelConsoleWriteLine("memory-test=pass");
}

void UmicomKernelMain(UmiU64 legacyHartId, UmiAddress legacyDeviceTreeAddress)
{
    /* Convert the historical K1/K2 ABI types to canonical K3 full-name types. */
    const UmicomU64 hartId = (UmicomU64)legacyHartId;

    /* Convert the historical address alias without changing its numeric value. */
    const UmicomAddress deviceTreeAddress =
        (UmicomAddress)legacyDeviceTreeAddress;

    /* Initialize the polling UART before any boot/trap/memory evidence is printed. */
    UmicomKernelConsoleInitialize();

    /* Preserve the original K1 begin marker as a regression contract. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BEGIN");

    /* Identify the running image. */
    WriteKeyValue("name", UMICOM_KERNEL_NAME);
    WriteKeyValue("version", UMICOM_KERNEL_VERSION);
    WriteKeyValue("milestone", UMICOM_KERNEL_MILESTONE);
    WriteKeyValue("arch", UMICOM_KERNEL_ARCHITECTURE);
    WriteKeyValue("machine", UMICOM_KERNEL_MACHINE);
    WriteKeyValue("build", UMICOM_KERNEL_BUILD_ID);

    /* Publish the boot hart selected by boot.S. */
    WriteUnsignedRecord("hart", hartId);

    /* Publish QEMU's DTB address before K3 validates its standard header. */
    WriteHexRecord("dtb", (UmicomU64)deviceTreeAddress);

    /* Preserve K1's exact boot-success field so the K1 CTest remains useful. */
    UmicomKernelConsoleWriteLine("state=booted");

    /* Install the direct machine-mode trap vector while interrupts are disabled. */
    UmicomRiscvTrapInstall();

    /* Publish the exact vector address placed into mtvec. */
    WriteHexRecord(
        "trap-vector",
        (UmicomU64)UmicomRiscvTrapVectorAddress()
    );

    /* Prove synchronous exception entry, decoding, mepc adjustment and MRET. */
    RunExceptionTest();

    /* Prove asynchronous machine-timer entry, acknowledgement and MRET. */
    RunTimerTest(hartId);

    /* Preserve the K2 capability marker after both trap classes pass. */
    UmicomKernelConsoleWriteLine("state=trap-timer-ready");

    /* Preserve the K2 CTest acceptance marker as a regression contract. */
    UmicomKernelConsoleWriteLine("K2_TRAP_TIMER_PASS");

    /* Run K3 only after the earlier CPU-control foundations remain healthy. */
    RunPhysicalMemoryTest(deviceTreeAddress);

    /* Publish the stronger K3 state only after all allocator checks pass. */
    UmicomKernelConsoleWriteLine("state=physical-memory-ready");

    /* This marker is the new K3 CTest acceptance expression. */
    UmicomKernelConsoleWriteLine("K3_PHYSICAL_MEMORY_PASS");

    /* Preserve the general end-of-evidence marker established by K1. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");

    /* End the QEMU run as a successful real-emulator test. */
    UmicomPlatformFinishSuccess();

    /* Keep a final safety halt if the emulator does not honour its test device. */
    UmicomPlatformHalt();
}
