/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/mapped_region_validation.c
 *
 * PURPOSE:
 *   Exercise owned mapped regions through the real existing supervisor monitor,
 *   including an actual downward stack overflow and non-executable stack fault.
 *
 * All addresses for the guest are selected by this trusted harness. Observed
 * causes, PCs and stack pointers must match the selected operation. A printed
 * marker is never substituted for a hardware observation. Existing services
 * and stacks are not migrated by this test.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/mapped_regions.h"
#include "umicom/kernel/riscv64/mapped_region_probe.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/console.h"

static UmicomKernelMappedSpace umicomRegionValidationSpace;
static UmicomU64 umicomRegionChecks;
#define UMICOM_REGION_STACK_BASE ((UmicomAddress)0x1000040000ULL)
#define UMICOM_REGION_OBSERVATION_BASE ((UmicomAddress)0x1000080000ULL)

/* Clear caller-owned output records without depending on a hosted memset.
 * Passing a mutable pointer also makes the initialisation boundary explicit. */
static void UmicomRegionZero(void *object, UmicomSize bytes)
{
    volatile UmicomU8 *const data = (volatile UmicomU8 *)object;
    for (UmicomSize index = 0U; index < bytes; ++index) data[index] = 0U;
}
static void UmicomRegionRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomRegionChecks;
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWrite("mapped-regions.failure=");
        UmicomKernelConsoleWriteLine(reason);
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(83U);
    }
}
#define UMICOM_REGION_REQUIRE(value, reason) UmicomRegionRequire((value) ? UMICOM_TRUE : UMICOM_FALSE, reason)
static void UmicomRegionHex(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWriteHex64(value);
    UmicomKernelConsoleWriteLine("");
}
static UmicomBoolean UmicomRegionMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    /* Pending timer bits are not borrowed state; all of these saved controls are. */
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomRegionRun(UmicomKernelMappedRegionHandle observations, UmicomU64 operation,
    UmicomU64 expectedCause, UmicomAddress expectedPc, UmicomAddress expectedValue, const char *name)
{
    UmicomKernelConsoleWrite("mapped-regions.case=");
    UmicomKernelConsoleWriteLine(name);
    UmicomKernelRegionObservation data;
    UmicomRegionZero(&data, sizeof(data));
    data.operation = operation;
    data.stackBase = UMICOM_REGION_STACK_BASE;
    data.lowerGuard = UMICOM_REGION_STACK_BASE - 4096U;
    data.upperGuard = UMICOM_REGION_STACK_BASE + 8192U;
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionWrite(&umicomRegionValidationSpace, observations,
        0U, &data, sizeof(data)) == UMICOM_MAPPED_OK, "observation-write");
    UmicomKernelMappedLease lease;
    UmicomRegionZero(&lease, sizeof(lease));
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceBorrow(&umicomRegionValidationSpace, &lease) == UMICOM_MAPPED_OK,
        "borrow-root");
    /* A root handed to the execution path must remain alive even though the
     * Kernel dispatcher is no longer executing its normal C call chain. */
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceClose(&umicomRegionValidationSpace) == UMICOM_MAPPED_BAD_STATE,
        "borrowed-close-refusal");
    UmicomPlatformPhysicalMemoryInfo ram;
    UmicomRegionZero(&ram, sizeof(ram));
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    UMICOM_REGION_REQUIRE(ram.bytes >= 8U && (ram.bytes & (ram.bytes - 1U)) == 0U &&
        (ram.base & (ram.bytes - 1U)) == 0U, "ram-napot-profile");
    const UmicomRiscvSupervisorRequest request = {lease.root,
        (UmicomAddress)(UmicomUIntPtr)UmicomRiscvRegionPayloadEntry,
        UMICOM_REGION_STACK_BASE + 8192U, UMICOM_REGION_OBSERVATION_BASE,
        ((UmicomU64)ram.base >> 2U) | ((ram.bytes >> 3U) - 1U)};
    UmicomRiscvSupervisorReport report;
    UmicomRegionZero(&report, sizeof(report));
    UmicomRiscvSupervisorMachineState before;
    UmicomRegionZero(&before, sizeof(before));
    UmicomRiscvSupervisorMachineState after;
    UmicomRegionZero(&after, sizeof(after));
    UmicomRiscvSupervisorMachineStateRead(&before);
    UMICOM_REGION_REQUIRE(UmicomRiscvSupervisorExecute(&request, &report) == 0U, "supervisor-entry");
    UmicomRiscvSupervisorMachineStateRead(&after);
    UMICOM_REGION_REQUIRE(UmicomRegionMachineEqual(&before, &after) != UMICOM_FALSE, "machine-restoration");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceReturn(&umicomRegionValidationSpace, &lease) == UMICOM_MAPPED_OK,
        "return-root");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceReturn(&umicomRegionValidationSpace, &lease) == UMICOM_MAPPED_INVALID_LEASE,
        "returned-ticket-refusal");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionRead(&umicomRegionValidationSpace, observations,
        0U, &data, sizeof(data)) == UMICOM_MAPPED_OK, "observation-read");
    UmicomRegionHex("mapped-regions.cause=", report.cause);
    UmicomRegionHex("mapped-regions.pc=", report.programCounter);
    UmicomRegionHex("mapped-regions.fault-address=", report.trapValue);
    UmicomRegionHex("mapped-regions.interrupted-stack=", report.interruptedStack);
    UMICOM_REGION_REQUIRE(report.cause == expectedCause && report.programCounter == expectedPc &&
        report.trapValue == expectedValue && ((report.machineStatus >> 11U) & 3U) == 1U,
        "actual-fault-or-completion");
    UMICOM_REGION_REQUIRE(data.entered == 1U && data.result == 160U &&
        data.observedStack >= UMICOM_REGION_STACK_BASE && data.observedStack < UMICOM_REGION_STACK_BASE + 8192U &&
        data.observedSatp == (0x8000000000000000ULL | ((UmicomU64)lease.root >> 12U)), "translated-stack-work");
    if (operation == 0U) {
        UMICOM_REGION_REQUIRE(data.completed == 1U && report.returnValue == 160U &&
            report.completionCookie == UMICOM_REGION_PROBE_COOKIE, "normal-completion");
    } else {
        UMICOM_REGION_REQUIRE(data.completed == 0U, "fault-prevented-return");
        if (operation == 3U) UMICOM_REGION_REQUIRE(report.interruptedStack == UMICOM_REGION_STACK_BASE - 16U,
            "actual-stack-crossed-boundary");
    }
    UmicomKernelConsoleWriteLine("mapped-regions.case-result=pass");
}
void UmicomKernelMappedRegionsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("mapped-regions-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRegionZero(&before, sizeof(before));
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRegionZero(&after, sizeof(after));
    UMICOM_REGION_REQUIRE(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "frame-baseline");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceInitialize(&umicomRegionValidationSpace) == UMICOM_MAPPED_OK,
        "space-initialize");
    const UmicomAddress textBegin = (UmicomAddress)(UmicomUIntPtr)__umicom_region_text_start;
    const UmicomAddress textEnd = (UmicomAddress)(UmicomUIntPtr)__umicom_region_text_end;
    UMICOM_REGION_REQUIRE(textEnd > textBegin && ((textEnd - textBegin) & 4095U) == 0U &&
        textEnd - textBegin <= 16U * 4096U, "probe-text-pages");
    const UmicomKernelMappedRegionSpec code = {textBegin, (textEnd - textBegin) / 4096U,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE,
        UMICOM_FALSE, UMICOM_FALSE, __umicom_region_text_start, textEnd - textBegin};
    UmicomKernelMappedRegionHandle codeHandle = 0U;
    UmicomKernelMappedRegionHandle stackHandle = 0U;
    UmicomKernelMappedRegionHandle dataHandle = 0U;
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionAdd(&umicomRegionValidationSpace, &code, &codeHandle) == UMICOM_MAPPED_OK,
        "code-plan");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedStackAdd(&umicomRegionValidationSpace, UMICOM_REGION_STACK_BASE,
        2U, UMICOM_FALSE, &stackHandle) == UMICOM_MAPPED_OK, "guarded-stack-plan");
    const UmicomKernelMappedRegionSpec data = {UMICOM_REGION_OBSERVATION_BASE, 1U,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE,
        UMICOM_FALSE, UMICOM_FALSE, (const UmicomU8 *)0, 0U};
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionAdd(&umicomRegionValidationSpace, &data, &dataHandle) == UMICOM_MAPPED_OK,
        "shared-observation-plan");
    const UmicomKernelMappedRegionSpec collision = {UMICOM_REGION_STACK_BASE - 4096U, 1U,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ, UMICOM_FALSE, UMICOM_FALSE, (const UmicomU8 *)0, 0U};
    UmicomKernelMappedRegionHandle refused = 0x1234U;
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionAdd(&umicomRegionValidationSpace, &collision, &refused) ==
        UMICOM_MAPPED_OVERLAP && refused == 0x1234U, "guard-reservation");
    UMICOM_REGION_REQUIRE(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames, "planning-acquires-no-frames");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceBuild(&umicomRegionValidationSpace) == UMICOM_MAPPED_OK, "build-space");
    UmicomU8 byte = 0U;
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionWrite(&umicomRegionValidationSpace, codeHandle, 0U, &byte, 1U) ==
        UMICOM_MAPPED_ACCESS_DENIED, "code-is-not-writable");
    UmicomKernelMappedRegionInfo stack;
    UmicomRegionZero(&stack, sizeof(stack));
    UMICOM_REGION_REQUIRE(UmicomKernelMappedRegionQuery(&umicomRegionValidationSpace, stackHandle, &stack) == UMICOM_MAPPED_OK &&
        stack.reservedBegin == UMICOM_REGION_STACK_BASE - 4096U && stack.reservedEnd == UMICOM_REGION_STACK_BASE + 12288U &&
        (stack.permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) == 0U, "stack-geometry");
    UmicomKernelConsoleWriteLine("mapped-regions.guard-reservations=preserved");
    UmicomRegionRun(dataHandle, 0U, 9U, (UmicomAddress)UmicomRiscvRegionCompletion, 0U, "nested-stack-return");
    UmicomRegionRun(dataHandle, 1U, 13U, (UmicomAddress)UmicomRiscvRegionLowerInstruction,
        UMICOM_REGION_STACK_BASE - 4096U, "lower-guard-load");
    UmicomRegionRun(dataHandle, 2U, 15U, (UmicomAddress)UmicomRiscvRegionUpperInstruction,
        UMICOM_REGION_STACK_BASE + 8192U, "upper-guard-store");
    UmicomRegionRun(dataHandle, 3U, 15U, (UmicomAddress)UmicomRiscvRegionStackInstruction,
        UMICOM_REGION_STACK_BASE - 16U, "downward-stack-overflow");
    UmicomRegionRun(dataHandle, 4U, 12U, UMICOM_REGION_STACK_BASE,
        UMICOM_REGION_STACK_BASE, "non-executable-stack");
    UmicomRegionRun(dataHandle, 0U, 9U, (UmicomAddress)UmicomRiscvRegionCompletion, 0U, "return-after-faults");
    UMICOM_REGION_REQUIRE(UmicomKernelMappedSpaceClose(&umicomRegionValidationSpace) == UMICOM_MAPPED_OK, "close-space");
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRegionZero(&trapBefore, sizeof(trapBefore));
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRegionZero(&trapAfter, sizeof(trapAfter));
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UMICOM_REGION_REQUIRE(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U, "original-machine-trap");
    UMICOM_REGION_REQUIRE(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK &&
        UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.reservedFrames == after.reservedFrames &&
        before.freeFrames == after.freeFrames, "frame-restoration");
    UmicomKernelConsoleWriteLine("mapped-regions.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("mapped-regions.control-state=restored");
    UmicomKernelConsoleWriteLine("mapped-regions.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("mapped-regions.completed-cases=6");
    UmicomKernelConsoleWrite("mapped-regions.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomRegionChecks);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("mapped-regions-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_MAPPED_REGIONS_READY");
}
