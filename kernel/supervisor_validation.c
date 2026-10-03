/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/supervisor_validation.c
 *
 * PURPOSE:
 *   Validate a real supervisor-mode C execution and its protection boundaries
 *   without replacing the existing machine-mode Kernel startup or trap policy.
 *
 * EDUCATIONAL OVERVIEW:
 *   The previous hardware test translates one data access while instructions
 *   still run in machine mode. Here, MRET actually lowers privilege. The CPU
 *   then translates instructions, stack traffic and observation-page accesses.
 *
 *   We map only isolated payload text, a stack and two data views. The machine
 *   return frame and report are not mapped. A private, temporary trap vector
 *   switches back to the saved machine stack before it touches memory, captures
 *   the supervisor event, and restores the caller's control state.
 *
 *   Expected faults are evidence, not errors to hide. We check the exact cause,
 *   instruction, previous privilege and fault address. A normal run is repeated
 *   afterwards to prove recovery. All allocated frames are returned at the end.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Existing services remain the authority for memory ownership and diagnostics. */
#include "umicom/kernel/address.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/trap.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/virtual_memory.h"

/* These are virtual test addresses, not machine MMIO locations. They sit in
 * Sv39's lower canonical region, well away from physical Kernel addresses. */
#define UMICOM_SUPERVISOR_STACK_BASE ((UmicomAddress)0x0000001000010000ULL)
#define UMICOM_SUPERVISOR_OBSERVATION_BASE ((UmicomAddress)0x0000001000020000ULL)
#define UMICOM_SUPERVISOR_READONLY_BASE ((UmicomAddress)0x0000001000030000ULL)

/* The read-only view must never change this physical word. */
#define UMICOM_SUPERVISOR_READONLY_SEED ((UmicomU64)0x726561646f6e6c79ULL)

/* Canary words occupy the bottom of the stack page, below the C frame. */
#define UMICOM_SUPERVISOR_STACK_CANARY ((UmicomU64)0x535441434b534146ULL)

/* Hardware cause values are architectural encodings, not development labels. */
#define UMICOM_SUPERVISOR_CAUSE_ECALL ((UmicomU64)9U)
#define UMICOM_SUPERVISOR_CAUSE_LOAD_PAGE_FAULT ((UmicomU64)13U)
#define UMICOM_SUPERVISOR_CAUSE_STORE_PAGE_FAULT ((UmicomU64)15U)
#define UMICOM_SUPERVISOR_CAUSE_ILLEGAL_INSTRUCTION ((UmicomU64)2U)

/* Print a short record through the already established machine-mode console. */
static void UmicomKernelSupervisorRecord(const char *key, const char *value)
{
    /* Keep the same key=value transcript format used by the other validations. */
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteLine(value);
}

/* Numbers remain explicit rather than relying on a hosted printf runtime. */
static void UmicomKernelSupervisorNumber(const char *key, UmicomU64 value)
{
    /* Decimal output is convenient for cause codes and allocation counts. */
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}

/* Addresses use full-width hexadecimal so no high Sv39 bits disappear. */
static void UmicomKernelSupervisorHex(const char *key, UmicomU64 value)
{
    /* The existing console helper owns integer formatting. */
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteHex64(value);
    UmicomKernelConsoleWriteLine("");
}

/* A failed validation never continues to the public readiness marker. */
static void UmicomKernelSupervisorRequire(UmicomBoolean condition, const char *reason)
{
    /* Successful checks have no additional side effects. */
    if (condition != UMICOM_FALSE) {
        return;
    }

    /* Report the first unmet invariant while still in ordinary machine mode. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelSupervisorRecord("reason", reason);
    UmicomPlatformFinishFailure((UmicomU32)120U);

    /* QEMU normally exits at the previous call. A platform without its test
     * device must still not fall through and claim that this check passed. */
    for (;;) {
        UmicomPlatformHalt();
    }
}

/* Compare every CSR borrowed by the transition. Do not use memcmp: it would
 * compare padding and introduce a libc dependency into freestanding code. */
static UmicomBoolean UmicomKernelSupervisorStateEqual(
    const UmicomRiscvSupervisorMachineState *before,
    const UmicomRiscvSupervisorMachineState *after
)
{
    /* Each pair represents a caller-owned control setting restored by Assembly. */
    return (
        before->mstatus == after->mstatus &&
        before->mie == after->mie &&
        before->mtvec == after->mtvec &&
        before->mscratch == after->mscratch &&
        before->medeleg == after->medeleg &&
        before->mideleg == after->mideleg &&
        before->satp == after->satp &&
        before->pmpcfg0 == after->pmpcfg0 &&
        before->pmpaddr0 == after->pmpaddr0 &&
        before->mepc == after->mepc &&
        before->mcause == after->mcause &&
        before->mtval == after->mtval
    ) ? UMICOM_TRUE : UMICOM_FALSE;
}

/* Map a complete page and stop immediately if the existing mapper refuses it. */
static void UmicomKernelSupervisorMap(
    UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomAddress physicalAddress,
    UmicomKernelVirtualMemoryPermissions permissions
)
{
    /* The address space owns table frames; the data frames remain ours. */
    const UmicomKernelVirtualMemoryStatus status =
        UmicomKernelVirtualMemoryMapPage(space, virtualAddress, physicalAddress, permissions);

    /* Never enter supervisor mode with only part of its environment mapped. */
    UmicomKernelSupervisorRequire(
        status == UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-map"
    );
}

/* Allocate an ordinary physical frame without bypassing the existing allocator. */
static UmicomAddress UmicomKernelSupervisorAllocate(void)
{
    /* Initialise the output so error diagnostics cannot expose an indeterminate value. */
    UmicomAddress address = (UmicomAddress)0U;
    const UmicomKernelMemoryStatus status = UmicomKernelPhysicalMemoryAllocateFrame(&address);

    /* Allocation failure is not a reason to use an unowned frame. */
    UmicomKernelSupervisorRequire(
        status == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-frame-allocation"
    );
    /* The allocator transfers ownership, not a promise that old bytes were
     * erased. Clear our data pages explicitly before exposing them to the
     * payload. Volatile stores keep this independent of any libc memset. */
    volatile UmicomU64 *const words = (volatile UmicomU64 *)address;
    for (UmicomSize index = 0U;
         index < UMICOM_KERNEL_PAGE_SIZE / (UmicomSize)sizeof(UmicomU64);
         ++index) {
        words[index] = (UmicomU64)0U;
    }
    return address;
}

/* Exercise one transition. Everything this function prints is measured after
 * the private Assembly vector has returned control to machine-mode C. */
static void UmicomKernelSupervisorRunCase(
    const UmicomRiscvSupervisorRequest *request,
    volatile UmicomRiscvSupervisorObservation *observation,
    volatile UmicomU64 *readOnlyWord,
    volatile UmicomU64 *stackWords,
    UmicomU64 operation,
    UmicomU64 expectedCause,
    UmicomAddress expectedInstruction,
    const char *caseName
)
{
    /* Reset only caller-owned observations. No machine context is exposed here. */
    observation->operation = operation;
    observation->guardVirtualAddress = UMICOM_SUPERVISOR_STACK_BASE - UMICOM_KERNEL_PAGE_SIZE;
    observation->readOnlyVirtualAddress = UMICOM_SUPERVISOR_READONLY_BASE;
    observation->entered = (UmicomU64)0U;
    observation->observedSatp = (UmicomU64)0U;
    observation->observedStack = (UmicomAddress)0U;
    observation->stackResult = (UmicomU64)0U;
    observation->completed = (UmicomU64)0U;

    /* Put canaries below the usable C frame. They detect an unexpectedly deep
     * stack before we recycle this page for an unrelated Kernel object. */
    stackWords[0] = UMICOM_SUPERVISOR_STACK_CANARY;
    stackWords[1] = ~UMICOM_SUPERVISOR_STACK_CANARY;

    /* The physical word must survive both ordinary execution and a refused store. */
    *readOnlyWord = UMICOM_SUPERVISOR_READONLY_SEED;
    UmicomKernelSupervisorRecord("supervisor.case", caseName);

    /* Inspect control state before calling the privileged Assembly boundary. */
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    UmicomKernelSupervisorRequire(
        (before.mstatus & (UmicomU64)0x20008U) == 0U && before.mie == 0U
            ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-requires-machine-physical-interrupts-disabled"
    );

    /* The report stays on the machine stack. This test deliberately leaves that
     * stack out of the supervisor page table; the payload is still trusted code,
     * not a hostile process confined by a complete security policy. */
    UmicomRiscvSupervisorReport report;
    const UmicomU64 result = UmicomRiscvSupervisorExecute(request, &report);

    /* Snapshot immediately after return, before printing or deliberately trapping. */
    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomKernelSupervisorRequire(result == 0U ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-transition-refused");
    UmicomKernelSupervisorRequire(UmicomKernelSupervisorStateEqual(&before, &after),
        "supervisor-machine-state-not-restored");

    /* The hardware writes MPP on trap entry. MPP=01 proves the previous mode
     * was supervisor, even for a negative test that never reaches ECALL. */
    const UmicomU64 previousPrivilege = (report.machineStatus >> 11U) & (UmicomU64)3U;
    UmicomKernelSupervisorNumber("supervisor.previous-privilege", previousPrivilege);
    UmicomKernelSupervisorNumber("supervisor.trap.cause", report.cause);
    UmicomKernelSupervisorHex("supervisor.trap.pc", report.programCounter);
    UmicomKernelSupervisorHex("supervisor.trap.value", report.trapValue);
    UmicomKernelSupervisorRequire(previousPrivilege == 1U ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-wrong-previous-privilege");
    UmicomKernelSupervisorRequire(report.cause == expectedCause ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-wrong-trap-cause");
    UmicomKernelSupervisorRequire(report.programCounter == expectedInstruction ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-wrong-trap-instruction");

    /* The observation is read through its original physical address in M-mode;
     * the payload wrote it through a different supervisor virtual address. */
    const UmicomU64 expectedSatp =
        ((UmicomU64)8U << 60U) | ((UmicomU64)request->rootTablePhysicalAddress >> 12U);
    UmicomKernelSupervisorRequire(
        observation->entered == 1U && observation->observedSatp == expectedSatp &&
        observation->stackResult == (UmicomU64)0x404U
            ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-payload-observation"
    );
    UmicomKernelSupervisorRequire(
        observation->observedStack >= UMICOM_SUPERVISOR_STACK_BASE &&
        observation->observedStack < request->stackTopVirtualAddress &&
        report.interruptedStack >= UMICOM_SUPERVISOR_STACK_BASE &&
        report.interruptedStack <= request->stackTopVirtualAddress
            ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-stack-outside-mapping"
    );

    /* Report what S-mode itself observed rather than only the machine-side
     * configuration that we hoped it would use. */
    UmicomKernelSupervisorHex("supervisor.observed-satp", observation->observedSatp);
    UmicomKernelSupervisorHex("supervisor.observed-stack", observation->observedStack);
    UmicomKernelSupervisorHex("supervisor.stack-result", observation->stackResult);

    /* A genuine normal return must carry both the C result and the Assembly
     * completion cookie. Fault cases must not pretend the C function finished. */
    if (operation == UMICOM_SUPERVISOR_OPERATION_RETURN) {
        UmicomKernelSupervisorRequire(
            observation->completed == 1U && report.returnValue == (UmicomU64)0x404U &&
            report.completionCookie == (UmicomU64)UMICOM_SUPERVISOR_COMPLETION_COOKIE
                ? UMICOM_TRUE : UMICOM_FALSE,
            "supervisor-normal-return"
        );
    } else {
        /* Every deliberate fault is placed after stack/observation checks but
         * before the payload sets its completed flag. */
        UmicomKernelSupervisorRequire(observation->completed == 0U ? UMICOM_TRUE : UMICOM_FALSE,
            "supervisor-fault-path-completed");
    }

    /* Page-fault mtval must identify the deliberate virtual access. Illegal
     * instruction mtval can be zero or instruction bits, so it is not guessed. */
    if (operation == UMICOM_SUPERVISOR_OPERATION_GUARD_LOAD) {
        UmicomKernelSupervisorRequire(report.trapValue == observation->guardVirtualAddress
            ? UMICOM_TRUE : UMICOM_FALSE, "supervisor-guard-fault-address");
    }
    if (operation == UMICOM_SUPERVISOR_OPERATION_READONLY_STORE) {
        UmicomKernelSupervisorRequire(report.trapValue == UMICOM_SUPERVISOR_READONLY_BASE
            ? UMICOM_TRUE : UMICOM_FALSE, "supervisor-readonly-fault-address");
    }

    /* Check physical effects as well as trap codes. A refused write must not
     * alter its target, and the translated C frame must not overrun its page. */
    UmicomKernelSupervisorRequire(*readOnlyWord == UMICOM_SUPERVISOR_READONLY_SEED
        ? UMICOM_TRUE : UMICOM_FALSE, "supervisor-readonly-data-modified");
    UmicomKernelSupervisorRequire(
        stackWords[0] == UMICOM_SUPERVISOR_STACK_CANARY &&
        stackWords[1] == ~UMICOM_SUPERVISOR_STACK_CANARY ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-stack-canary"
    );
    UmicomKernelSupervisorRecord("supervisor.machine-state", "restored");
    UmicomKernelSupervisorRecord("supervisor.case-result", "pass");
}

void UmicomKernelSupervisorExecutionValidate(void)
{
    /* Preserve all earlier capability checks; this function is appended after
     * them rather than replacing any existing Kernel validation path. */
    UmicomKernelConsoleWriteLine("supervisor-execution-test=begin");

    /* Keep the allocator baseline so every temporary ownership claim can be
     * checked after the page tables, data views and stack are destroyed. */
    UmicomKernelPhysicalMemorySnapshot baseline;
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemorySnapshotRead(&baseline)
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-baseline-accounting");

    /* NAPOT describes a power-of-two, naturally aligned physical region.
     * Derive it from the existing allocator's RAM geometry, not a duplicate
     * machine-address constant hidden in supervisor Assembly. */
    const UmicomU64 ramBytes = (UmicomU64)baseline.ramBytes;
    UmicomKernelSupervisorRequire(
        ramBytes >= 8U && (ramBytes & (ramBytes - 1U)) == 0U &&
        (((UmicomU64)baseline.ramBase) & (ramBytes - 1U)) == 0U
            ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-ram-not-napot-compatible"
    );

    /* Start with a visibly empty address-space owner, using the existing API. */
    UmicomKernelVirtualAddressSpace space = {
        (UmicomAddress)0U, (UmicomSize)0U, (UmicomSize)0U, UMICOM_FALSE
    };
    UmicomKernelSupervisorRequire(UmicomKernelVirtualAddressSpaceCreate(&space)
        == UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-address-space-create");

    /* These three data frames stay caller-owned. Address-space destruction
     * returns only its own page-table frames, never these allocations. */
    const UmicomAddress observationFrame = UmicomKernelSupervisorAllocate();
    const UmicomAddress stackFrame = UmicomKernelSupervisorAllocate();
    const UmicomAddress readOnlyFrame = UmicomKernelSupervisorAllocate();

    /* The linker isolates payload text onto complete pages. Check the contract
     * before mapping so an alignment error cannot expose neighbouring code. */
    const UmicomAddress textStart = (UmicomAddress)__umicom_supervisor_text_start;
    const UmicomAddress textEnd = (UmicomAddress)__umicom_supervisor_text_end;
    const UmicomAddress pageMask = (UmicomAddress)UMICOM_KERNEL_PAGE_SIZE - 1U;
    UmicomKernelSupervisorRequire(textEnd > textStart &&
        (textStart & pageMask) == 0U && (textEnd & pageMask) == 0U
            ? UMICOM_TRUE : UMICOM_FALSE, "supervisor-text-boundary");

    /* Only the payload's own text is visible for S-mode instruction fetch.
     * Mapping at the link address keeps normal C/Assembly calls meaningful. */
    for (UmicomAddress page = textStart; page < textEnd; page += UMICOM_KERNEL_PAGE_SIZE) {
        UmicomKernelSupervisorMap(&space, page, page,
            UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE);
    }
    UmicomKernelSupervisorMap(&space, UMICOM_SUPERVISOR_STACK_BASE, stackFrame,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE);
    UmicomKernelSupervisorMap(&space, UMICOM_SUPERVISOR_OBSERVATION_BASE, observationFrame,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE);
    UmicomKernelSupervisorMap(&space, UMICOM_SUPERVISOR_READONLY_BASE, readOnlyFrame,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ);

    /* There is intentionally no mapping immediately below the stack page. */
    UmicomAddress ignoredPhysical = (UmicomAddress)0U;
    UmicomKernelSupervisorRequire(UmicomKernelVirtualMemoryTranslate(&space,
        UMICOM_SUPERVISOR_STACK_BASE - UMICOM_KERNEL_PAGE_SIZE, &ignoredPhysical,
        (UmicomKernelVirtualMemoryPermissions *)0) == UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED
            ? UMICOM_TRUE : UMICOM_FALSE, "supervisor-guard-was-mapped");
    UmicomKernelSupervisorRequire(UmicomKernelVirtualMemoryValidate(&space)
        == UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-page-table-validation");

    /* Assemble the request only after the complete environment is valid. */
    UmicomRiscvSupervisorRequest request;
    request.rootTablePhysicalAddress = space.rootTablePhysicalAddress;
    request.entryVirtualAddress = (UmicomAddress)UmicomRiscvSupervisorEntry;
    request.stackTopVirtualAddress = UMICOM_SUPERVISOR_STACK_BASE + UMICOM_KERNEL_PAGE_SIZE;
    request.observationVirtualAddress = UMICOM_SUPERVISOR_OBSERVATION_BASE;
    request.pmpNapotAddress = ((UmicomU64)baseline.ramBase >> 2U) | ((ramBytes >> 3U) - 1U);
    UmicomKernelSupervisorHex("supervisor.root-table", request.rootTablePhysicalAddress);
    UmicomKernelSupervisorHex("supervisor.text.start", textStart);
    UmicomKernelSupervisorHex("supervisor.text.end", textEnd);

    /* Machine mode inspects the frames physically after each translated run. */
    volatile UmicomRiscvSupervisorObservation *const observation =
        (volatile UmicomRiscvSupervisorObservation *)observationFrame;
    volatile UmicomU64 *const stackWords = (volatile UmicomU64 *)stackFrame;
    volatile UmicomU64 *const readOnlyWord = (volatile UmicomU64 *)readOnlyFrame;

    /* Prove ordinary C execution first, then protection faults, then ordinary
     * execution again so recovery is tested rather than merely claimed. */
    UmicomKernelSupervisorRunCase(&request, observation, readOnlyWord, stackWords,
        UMICOM_SUPERVISOR_OPERATION_RETURN, UMICOM_SUPERVISOR_CAUSE_ECALL,
        (UmicomAddress)UmicomRiscvSupervisorCompletionInstruction, "normal-return");
    UmicomKernelSupervisorRunCase(&request, observation, readOnlyWord, stackWords,
        UMICOM_SUPERVISOR_OPERATION_GUARD_LOAD, UMICOM_SUPERVISOR_CAUSE_LOAD_PAGE_FAULT,
        (UmicomAddress)UmicomRiscvSupervisorGuardLoadInstruction, "unmapped-guard-load");
    UmicomKernelSupervisorRunCase(&request, observation, readOnlyWord, stackWords,
        UMICOM_SUPERVISOR_OPERATION_READONLY_STORE, UMICOM_SUPERVISOR_CAUSE_STORE_PAGE_FAULT,
        (UmicomAddress)UmicomRiscvSupervisorReadOnlyStoreInstruction, "read-only-store");
    UmicomKernelSupervisorRunCase(&request, observation, readOnlyWord, stackWords,
        UMICOM_SUPERVISOR_OPERATION_MACHINE_CSR, UMICOM_SUPERVISOR_CAUSE_ILLEGAL_INSTRUCTION,
        (UmicomAddress)UmicomRiscvSupervisorMachineStatusInstruction, "machine-csr-refusal");
    UmicomKernelSupervisorRunCase(&request, observation, readOnlyWord, stackWords,
        UMICOM_SUPERVISOR_OPERATION_RETURN, UMICOM_SUPERVISOR_CAUSE_ECALL,
        (UmicomAddress)UmicomRiscvSupervisorCompletionInstruction, "normal-return-after-faults");

    /* Check the original machine trap handler still works after restoring mtvec.
     * Its existing code remains untouched; this is a new caller of that API. */
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomKernelSupervisorRequire(
        trapAfter.exceptionCount == trapBefore.exceptionCount + 1U &&
        trapAfter.timerInterruptCount == trapBefore.timerInterruptCount &&
        trapAfter.lastCauseCode == UMICOM_RISCV_EXCEPTION_ECALL_M_MODE &&
        trapAfter.lastWasInterrupt == 0U ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-original-machine-trap-not-restored"
    );
    UmicomKernelSupervisorRecord("supervisor.original-trap-handler", "pass");

    /* The Assembly boundary has restored the original satp before we release
     * any page table. No CPU may keep walking a frame that has been freed. */
    UmicomKernelSupervisorRequire(UmicomKernelVirtualAddressSpaceDestroy(&space)
        == UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-address-space-destroy");
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemoryFreeFrame(readOnlyFrame)
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-readonly-frame-free");
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemoryFreeFrame(stackFrame)
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-stack-frame-free");
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemoryFreeFrame(observationFrame)
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-observation-frame-free");

    /* Recount the allocator independently, then compare with its original
     * ownership totals. Passing traps must not hide leaked page-table frames. */
    UmicomKernelPhysicalMemorySnapshot finalState;
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemoryValidate()
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-final-allocator-invariant");
    UmicomKernelSupervisorRequire(UmicomKernelPhysicalMemorySnapshotRead(&finalState)
        == UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-final-accounting");
    UmicomKernelSupervisorRequire(
        finalState.allocatedFrames == baseline.allocatedFrames &&
        finalState.reservedFrames == baseline.reservedFrames &&
        finalState.freeFrames == baseline.freeFrames ? UMICOM_TRUE : UMICOM_FALSE,
        "supervisor-frame-leak"
    );
    UmicomKernelSupervisorRecord("supervisor.frame-accounting", "restored");
    UmicomKernelSupervisorNumber("supervisor.completed-cases", (UmicomU64)5U);
    UmicomKernelConsoleWriteLine("supervisor-execution-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_SUPERVISOR_EXECUTION_READY");
}
