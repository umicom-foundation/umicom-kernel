/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_validation.c
 *
 * PURPOSE:
 *   Validate two independent U-mode address spaces, resumable environment calls,
 *   user-buffer refusal paths, page permissions and bounded execution recovery.
 *
 * EDUCATIONAL OVERVIEW:
 *   Both tasks see exactly the same virtual layout. Their writable pages and
 *   page-table roots are different physical allocations. Running A, B and A
 *   again tests isolation by observing persistent data, not just different IDs.
 *
 *   Only the built-in user text is shared. It is RX with the USER bit set.
 *   Stack/data pages are RW and never executable. A read-only page and a mapped
 *   supervisor-only page let the processor prove the two permission boundaries.
 *   Nothing in the existing supervisor or machine trap implementations is
 *   replaced; each user run restores the borrowed machine control state.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/address.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/user_execution.h"
#include "umicom/kernel/user_observation.h"

/* These values are data-integrity sentinels, not protocol or release versions. */
#define UMICOM_USER_STACK_CANARY ((UmicomU64)0x736166657374616bULL)
#define UMICOM_USER_PROTECTED_SEED ((UmicomU64)0x70726f7465637464ULL)
/* QEMU's selected CLINT profile runs at 10 MHz; allow 100 ms per invocation. */
#define UMICOM_USER_DEADLINE_TICKS ((UmicomU64)1000000U)

typedef struct UmicomKernelUserTask {
    UmicomKernelVirtualAddressSpace space; /* Owns the page-table frames only. */
    UmicomKernelUserPage pages[UMICOM_USER_MEMORY_MAX_PAGES];
    UmicomSize pageCount;
    UmicomAddress stack;
    UmicomAddress dataFirst;
    UmicomAddress dataSecond;
    UmicomAddress readOnly;
    UmicomAddress supervisorOnly;
    UmicomU64 identity;
} UmicomKernelUserTask;

/* Separate owners are intentional: one task must never borrow another's root. */
static UmicomKernelUserTask umicomUserTaskA;
static UmicomKernelUserTask umicomUserTaskB;

/* Validation errors are Kernel failures, unlike an expected user page fault. */
static void UmicomKernelUserRequire(UmicomBoolean condition, const char *reason)
{
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(170U);
        UmicomPlatformHalt();
    }
}

static void UmicomKernelUserDecimal(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}

static void UmicomKernelUserHex(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteHex64(value);
    UmicomKernelConsoleWriteLine("");
}

/* Volatile byte stores prevent an optimising compiler from synthesising a
 * hosted memset dependency in this freestanding acceptance code. */
static void UmicomKernelUserZero(void *storage, UmicomSize bytes)
{
    volatile UmicomU8 *const target = (volatile UmicomU8 *)storage;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        target[index] = 0U;
    }
}

static UmicomAddress UmicomKernelUserAllocate(void)
{
    UmicomAddress address = 0U;
    UmicomKernelUserRequire(
        UmicomKernelPhysicalMemoryAllocateFrame(&address) == UMICOM_KERNEL_MEMORY_OK
            ? UMICOM_TRUE : UMICOM_FALSE, "user-frame-allocation");
    UmicomKernelUserZero((void *)address, UMICOM_KERNEL_PAGE_SIZE);
    return address;
}

/* Record only mappings actually created through the existing mapper. The
 * backing records stay on the machine side and cannot be changed by the user. */
static void UmicomKernelUserMap(
    UmicomKernelUserTask *task,
    UmicomAddress virtualBase,
    UmicomAddress physicalBase,
    UmicomKernelVirtualMemoryPermissions permissions
)
{
    UmicomKernelUserRequire(task->pageCount < UMICOM_USER_MEMORY_MAX_PAGES
        ? UMICOM_TRUE : UMICOM_FALSE, "user-owner-record-capacity");
    UmicomKernelUserRequire(UmicomKernelVirtualMemoryMapPage(&task->space,
        virtualBase, physicalBase, permissions) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "user-map");
    UmicomKernelUserPage *const page = &task->pages[task->pageCount];
    page->virtualBase = virtualBase;
    page->physicalBase = physicalBase;
    page->permissions = permissions;
    ++task->pageCount;
}

static void UmicomKernelUserTaskCreate(UmicomKernelUserTask *task, UmicomU64 identity)
{
    /* BSS made this owner empty at boot. Refuse to silently overwrite ownership. */
    UmicomKernelUserRequire(task->space.initialised == UMICOM_FALSE
        ? UMICOM_TRUE : UMICOM_FALSE, "user-owner-already-live");
    task->identity = identity;
    UmicomKernelUserRequire(UmicomKernelVirtualAddressSpaceCreate(&task->space) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-root-create");

    /* Allocate a different object between the data pages. The cross-page copy
     * must follow PTEs rather than assuming physically adjacent backing. */
    task->dataFirst = UmicomKernelUserAllocate();
    task->readOnly = UmicomKernelUserAllocate();
    task->dataSecond = UmicomKernelUserAllocate();
    task->stack = UmicomKernelUserAllocate();
    task->supervisorOnly = UmicomKernelUserAllocate();
    UmicomKernelUserRequire(task->dataSecond != task->dataFirst + UMICOM_KERNEL_PAGE_SIZE
        ? UMICOM_TRUE : UMICOM_FALSE, "user-copy-fixture-must-be-discontiguous");

    /* Both tasks share only the immutable executable payload frames. */
    const UmicomAddress textStart = (UmicomAddress)__umicom_user_text_start;
    const UmicomAddress textEnd = (UmicomAddress)__umicom_user_text_end;
    UmicomKernelUserRequire(textEnd > textStart &&
        (textStart & (UMICOM_KERNEL_PAGE_SIZE - 1U)) == 0U &&
        (textEnd & (UMICOM_KERNEL_PAGE_SIZE - 1U)) == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-text-range");
    for (UmicomAddress address = textStart; address < textEnd;
         address += (UmicomAddress)UMICOM_KERNEL_PAGE_SIZE) {
        UmicomKernelUserMap(task, address, address,
            UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE);
    }
    const UmicomKernelVirtualMemoryPermissions writable =
        UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
        UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE;
    UmicomKernelUserMap(task, UMICOM_USER_STACK_BASE, task->stack, writable);
    UmicomKernelUserMap(task, UMICOM_USER_DATA_BASE, task->dataFirst, writable);
    UmicomKernelUserMap(task, UMICOM_USER_DATA_BASE + UMICOM_USER_PAGE_BYTES,
        task->dataSecond, writable);
    UmicomKernelUserMap(task, UMICOM_USER_READONLY_BASE, task->readOnly,
        UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    /* Intentionally omit USER. Presence alone must not grant user access. */
    UmicomKernelUserMap(task, UMICOM_USER_SUPERVISOR_BASE, task->supervisorOnly,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ);

    *(volatile UmicomU64 *)task->readOnly = UMICOM_USER_PROTECTED_SEED;
    *(volatile UmicomU64 *)task->supervisorOnly = UMICOM_USER_PROTECTED_SEED;
    *(volatile UmicomU64 *)task->stack = UMICOM_USER_STACK_CANARY;
    UmicomKernelUserRequire(UmicomKernelVirtualMemoryValidate(&task->space) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-page-table-validation");
}

/* The existing supervisor reader is reusable for the common borrowed CSRs.
 * User execution additionally borrows counter permissions, checked separately. */
static UmicomBoolean UmicomKernelUserMachineStateEqual(
    const UmicomRiscvSupervisorMachineState *left,
    const UmicomRiscvSupervisorMachineState *right
)
{
    return left->mstatus == right->mstatus && left->mie == right->mie &&
        left->mtvec == right->mtvec && left->mscratch == right->mscratch &&
        left->medeleg == right->medeleg && left->mideleg == right->mideleg &&
        left->satp == right->satp && left->pmpcfg0 == right->pmpcfg0 &&
        left->pmpaddr0 == right->pmpaddr0 && left->mepc == right->mepc &&
        left->mcause == right->mcause && left->mtval == right->mtval
        ? UMICOM_TRUE : UMICOM_FALSE;
}

/* The RAM-only NAPOT expression is derived from validated geometry, not a
 * blanket all-physical-address permission. No MMIO is granted to user code. */
static UmicomU64 UmicomKernelUserPmpAddress(void)
{
    UmicomPlatformPhysicalMemoryInfo memory;
    UmicomPlatformPhysicalMemoryDescribe(&memory);
    UmicomKernelUserRequire(memory.bytes >= 8U &&
        (memory.bytes & (memory.bytes - 1U)) == 0U &&
        (memory.base & (UmicomAddress)(memory.bytes - 1U)) == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-pmp-geometry");
    return ((UmicomU64)memory.base >> 2U) | ((memory.bytes >> 3U) - 1U);
}

/* Prepare only test scratch bytes, preserving the per-task persistent counter. */
static void UmicomKernelUserPrepareBuffers(UmicomKernelUserTask *task)
{
    volatile UmicomU8 *const first = (volatile UmicomU8 *)task->dataFirst;
    volatile UmicomU8 *const second = (volatile UmicomU8 *)task->dataSecond;
    for (UmicomSize index = 0U; index < 8U; ++index) {
        first[UMICOM_KERNEL_PAGE_SIZE - 8U + index] = (UmicomU8)(0x40U + index);
        second[index] = (UmicomU8)(0x48U + index);
        second[UMICOM_KERNEL_PAGE_SIZE - 8U + index] = (UmicomU8)0xccU;
    }
    for (UmicomSize index = 0U; index < 16U; ++index) {
        first[128U + index] = 0U;
    }
}

static void UmicomKernelUserRunCase(
    UmicomKernelUserTask *task,
    const char *name,
    UmicomU64 operation,
    UmicomKernelUserStopReason expectedReason,
    UmicomU64 expectedCause,
    UmicomAddress expectedPc,
    UmicomAddress expectedFaultAddress
)
{
    UmicomKernelConsoleWrite("user.case=");
    UmicomKernelConsoleWriteLine(name);
    UmicomKernelUserDecimal("user.identity", task->identity);
    UmicomKernelUserPrepareBuffers(task);

    /* A fresh report cannot inherit success fields from the previous invocation. */
    UmicomKernelUserSession session;
    UmicomKernelUserZero(&session, sizeof(session));
    session.memory.space = &task->space;
    session.memory.pages = task->pages;
    session.memory.pageCount = task->pageCount;
    session.identity = task->identity;
    UmicomRiscvUserRequest request;
    request.rootTablePhysicalAddress = task->space.rootTablePhysicalAddress;
    request.entryVirtualAddress = (UmicomAddress)UmicomRiscvUserEntry;
    request.stackTopVirtualAddress = UMICOM_USER_STACK_BASE + UMICOM_USER_PAGE_BYTES;
    request.argument = operation;
    request.pmpNapotAddress = UmicomKernelUserPmpAddress();

    /* Check executable entry and the entire writable stack before lowering
     * privilege. The machine return frame is never present in these records. */
    UmicomKernelUserRequire(UmicomKernelUserMemoryCheck(&session.memory,
        request.entryVirtualAddress, 4U, UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) ==
        UMICOM_USER_RESULT_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-entry-mapping");
    UmicomKernelUserRequire(UmicomKernelUserMemoryCheck(&session.memory,
        UMICOM_USER_STACK_BASE, UMICOM_USER_PAGE_BYTES, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) ==
        UMICOM_USER_RESULT_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-stack-mapping");

    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    UmicomKernelUserRequire(before.mie == 0U && (before.mstatus & 0x20008U) == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-machine-entry-policy");
    UmicomU64 counterBefore = 0U;
    UmicomU64 supervisorCounterBefore = 0U;
    __asm__ volatile("csrr %0, mcounteren" : "=r"(counterBefore));
    __asm__ volatile("csrr %0, scounteren" : "=r"(supervisorCounterBefore));
    const UmicomU64 oldCompare = UmicomPlatformTimerCompareRead(0U);
    const UmicomU64 now = UmicomPlatformTimerRead();
    UmicomKernelUserRequire(now <= ~(UmicomU64)0U - UMICOM_USER_DEADLINE_TICKS
        ? UMICOM_TRUE : UMICOM_FALSE, "user-deadline-overflow");
    UmicomPlatformTimerSetCompare(0U, now + UMICOM_USER_DEADLINE_TICKS);

    /* This one call may resume many system calls, but always returns on a
     * terminal event. A busy loop is stopped by the independent timer source. */
    const UmicomU64 entryResult = UmicomRiscvUserExecute(&request, &session);
    UmicomPlatformTimerSetCompare(0U, oldCompare);
    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomU64 counterAfter = 0U;
    UmicomU64 supervisorCounterAfter = 0U;
    __asm__ volatile("csrr %0, mcounteren" : "=r"(counterAfter));
    __asm__ volatile("csrr %0, scounteren" : "=r"(supervisorCounterAfter));

    /* Restore and verify machine state before interpreting an expected fault. */
    UmicomKernelUserRequire(entryResult == 0U ? UMICOM_TRUE : UMICOM_FALSE, "user-entry-refused");
    UmicomKernelUserRequire(UmicomKernelUserMachineStateEqual(&before, &after),
        "user-machine-state-not-restored");
    UmicomKernelUserRequire(counterBefore == counterAfter &&
        supervisorCounterBefore == supervisorCounterAfter &&
        UmicomPlatformTimerCompareRead(0U) == oldCompare
        ? UMICOM_TRUE : UMICOM_FALSE, "user-counter-or-deadline-not-restored");
    UmicomKernelUserHex("user.trap.cause", session.trapCause);
    UmicomKernelUserHex("user.trap.pc", session.trapPc);
    UmicomKernelUserHex("user.trap.value", session.trapValue);
    UmicomKernelUserDecimal("user.previous-privilege", (session.trapStatus >> 11U) & 3U);
    UmicomKernelUserDecimal("user.syscalls", session.callCount);
    UmicomKernelUserDecimal("user.rejected-calls", session.rejectedCalls);
    UmicomKernelUserRequire(session.stopReason == expectedReason &&
        session.trapCause == expectedCause && ((session.trapStatus >> 11U) & 3U) == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-wrong-terminal-event");
    if (expectedPc != 0U) {
        UmicomKernelUserRequire(session.trapPc == expectedPc
            ? UMICOM_TRUE : UMICOM_FALSE, "user-wrong-fault-instruction");
    }
    if (expectedFaultAddress != 0U) {
        UmicomKernelUserRequire(session.trapValue == expectedFaultAddress
            ? UMICOM_TRUE : UMICOM_FALSE, "user-wrong-fault-address");
    }
    UmicomKernelUserRequire(*(const volatile UmicomU64 *)task->readOnly == UMICOM_USER_PROTECTED_SEED &&
        *(const volatile UmicomU64 *)task->supervisorOnly == UMICOM_USER_PROTECTED_SEED &&
        *(const volatile UmicomU64 *)task->stack == UMICOM_USER_STACK_CANARY
        ? UMICOM_TRUE : UMICOM_FALSE, "user-protected-data-or-canary-changed");

    const volatile UmicomKernelUserObservation *const observation =
        (const volatile UmicomKernelUserObservation *)task->dataFirst;
    UmicomKernelUserRequire(observation->identity == task->identity &&
        observation->stackResult == task->identity * 4U + 6U &&
        observation->stackAddress >= UMICOM_USER_STACK_BASE &&
        observation->stackAddress < UMICOM_USER_STACK_BASE + UMICOM_USER_PAGE_BYTES
        ? UMICOM_TRUE : UMICOM_FALSE, "user-stack-or-identity-observation");
    if (operation == UMICOM_USER_OPERATION_NORMAL) {
        UmicomKernelUserRequire(session.exitValue == 0U && observation->completed == 1U &&
            observation->syscallChecks == 10U && observation->registerCheck == 1U &&
            session.callCount == 13U && session.rejectedCalls == 8U && session.copiedBytes == 16U
            ? UMICOM_TRUE : UMICOM_FALSE, "user-system-call-validation");
        /* Verify the resulting physical bytes independently of the user flag. */
        const volatile UmicomU8 *const bytes = (const volatile UmicomU8 *)task->dataFirst;
        for (UmicomSize index = 0U; index < 16U; ++index) {
            UmicomKernelUserRequire(bytes[128U + index] == (UmicomU8)(0x40U + index)
                ? UMICOM_TRUE : UMICOM_FALSE, "user-copy-physical-observation");
        }
    } else if (operation == UMICOM_USER_OPERATION_INVALID_STACK_EXIT) {
        UmicomKernelUserRequire(session.exitValue == 0U &&
            session.trapStack == UMICOM_USER_STACK_BASE - UMICOM_USER_PAGE_BYTES
            ? UMICOM_TRUE : UMICOM_FALSE, "user-invalid-stack-exit");
    } else if (operation == UMICOM_USER_OPERATION_CALL_BUDGET) {
        UmicomKernelUserRequire(session.callCount == UMICOM_USER_CALL_LIMIT + 1U
            ? UMICOM_TRUE : UMICOM_FALSE, "user-call-budget");
    }
    UmicomKernelConsoleWriteLine("user.machine-state=restored");
    UmicomKernelConsoleWriteLine("user.case-result=pass");
}

static void UmicomKernelUserTaskDestroy(UmicomKernelUserTask *task)
{
    /* The machine root has already been restored. No CPU can walk these tables
     * after this point, so the existing destructor may reclaim their frames. */
    UmicomKernelUserRequire(UmicomKernelVirtualAddressSpaceDestroy(&task->space) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-root-destroy");
    const UmicomAddress frames[] = {
        task->dataFirst, task->dataSecond, task->stack, task->readOnly, task->supervisorOnly
    };
    for (UmicomSize index = 0U; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        UmicomKernelUserRequire(UmicomKernelPhysicalMemoryFreeFrame(frames[index]) ==
            UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-frame-free");
    }
}

void UmicomKernelUserExecutionValidate(void)
{
    UmicomKernelConsoleWriteLine("user-execution-test=begin");
    UmicomKernelPhysicalMemorySnapshot baseline;
    UmicomKernelUserRequire(UmicomKernelPhysicalMemorySnapshotRead(&baseline) ==
        UMICOM_KERNEL_MEMORY_OK ? UMICOM_TRUE : UMICOM_FALSE, "user-baseline");
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomKernelUserTaskCreate(&umicomUserTaskA, 101U);
    UmicomKernelUserTaskCreate(&umicomUserTaskB, 202U);
    UmicomKernelUserRequire(umicomUserTaskA.space.rootTablePhysicalAddress !=
        umicomUserTaskB.space.rootTablePhysicalAddress &&
        umicomUserTaskA.dataFirst != umicomUserTaskB.dataFirst
        ? UMICOM_TRUE : UMICOM_FALSE, "user-isolation-distinct-owners");
    UmicomKernelUserHex("user.root.a", umicomUserTaskA.space.rootTablePhysicalAddress);
    UmicomKernelUserHex("user.root.b", umicomUserTaskB.space.rootTablePhysicalAddress);
    UmicomKernelUserHex("user.data-physical.a", umicomUserTaskA.dataFirst);
    UmicomKernelUserHex("user.data-physical.b", umicomUserTaskB.dataFirst);

    UmicomKernelUserRunCase(&umicomUserTaskA, "task-a-system-calls", UMICOM_USER_OPERATION_NORMAL,
        UMICOM_USER_STOP_EXIT, 8U, (UmicomAddress)UmicomRiscvUserExitInstruction, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskB, "task-b-system-calls", UMICOM_USER_OPERATION_NORMAL,
        UMICOM_USER_STOP_EXIT, 8U, (UmicomAddress)UmicomRiscvUserExitInstruction, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskA, "task-a-reentry-isolation", UMICOM_USER_OPERATION_NORMAL,
        UMICOM_USER_STOP_EXIT, 8U, (UmicomAddress)UmicomRiscvUserExitInstruction, 0U);
    const volatile UmicomKernelUserObservation *const a =
        (const volatile UmicomKernelUserObservation *)umicomUserTaskA.dataFirst;
    const volatile UmicomKernelUserObservation *const b =
        (const volatile UmicomKernelUserObservation *)umicomUserTaskB.dataFirst;
    UmicomKernelUserRequire(a->runs == 2U && b->runs == 1U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-data-isolation");
    UmicomKernelConsoleWriteLine("user.address-space-isolation=pass");

    UmicomKernelUserRunCase(&umicomUserTaskA, "unmapped-guard-load", UMICOM_USER_OPERATION_GUARD_LOAD,
        UMICOM_USER_STOP_FAULT, 13U, (UmicomAddress)UmicomRiscvUserLoadInstruction,
        UMICOM_USER_STACK_BASE - UMICOM_USER_PAGE_BYTES);
    UmicomKernelUserRunCase(&umicomUserTaskA, "read-only-store", UMICOM_USER_OPERATION_READONLY_STORE,
        UMICOM_USER_STOP_FAULT, 15U, (UmicomAddress)UmicomRiscvUserStoreInstruction, UMICOM_USER_READONLY_BASE);
    UmicomKernelUserRunCase(&umicomUserTaskA, "supervisor-page-refusal", UMICOM_USER_OPERATION_SUPERVISOR_LOAD,
        UMICOM_USER_STOP_FAULT, 13U, (UmicomAddress)UmicomRiscvUserLoadInstruction, UMICOM_USER_SUPERVISOR_BASE);
    UmicomKernelUserRunCase(&umicomUserTaskA, "supervisor-csr-refusal", UMICOM_USER_OPERATION_SUPERVISOR_CSR,
        UMICOM_USER_STOP_FAULT, 2U, (UmicomAddress)UmicomRiscvUserSupervisorCsrInstruction, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskA, "non-executable-data", UMICOM_USER_OPERATION_NONEXECUTABLE,
        UMICOM_USER_STOP_FAULT, 12U, UMICOM_USER_DATA_BASE, UMICOM_USER_DATA_BASE);
    UmicomKernelUserRunCase(&umicomUserTaskA, "invalid-stack-exit", UMICOM_USER_OPERATION_INVALID_STACK_EXIT,
        UMICOM_USER_STOP_EXIT, 8U, (UmicomAddress)UmicomRiscvUserInvalidStackExitInstruction, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskA, "system-call-budget", UMICOM_USER_OPERATION_CALL_BUDGET,
        UMICOM_USER_STOP_CALL_BUDGET, 8U, 0U, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskA, "busy-loop-deadline", UMICOM_USER_OPERATION_BUSY_LOOP,
        UMICOM_USER_STOP_DEADLINE, UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U,
        (UmicomAddress)UmicomRiscvUserBusyInstruction, 0U);
    UmicomKernelUserRunCase(&umicomUserTaskB, "task-b-after-faults", UMICOM_USER_OPERATION_NORMAL,
        UMICOM_USER_STOP_EXIT, 8U, (UmicomAddress)UmicomRiscvUserExitInstruction, 0U);
    UmicomKernelUserRequire(a->runs == 2U && b->runs == 2U
        ? UMICOM_TRUE : UMICOM_FALSE, "user-isolation-after-faults");

    /* Private user traps and timer recovery must not alter the established
     * trap subsystem's counters. Then prove that original ECALL still returns. */
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomKernelUserRequire(trapAfter.exceptionCount == trapBefore.exceptionCount &&
        trapAfter.timerInterruptCount == trapBefore.timerInterruptCount
        ? UMICOM_TRUE : UMICOM_FALSE, "user-private-trap-accounting");
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomKernelUserRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U &&
        trapAfter.lastCauseCode == UMICOM_RISCV_EXCEPTION_ECALL_M_MODE
        ? UMICOM_TRUE : UMICOM_FALSE, "user-original-trap-handler");
    UmicomKernelConsoleWriteLine("user.original-trap-handler=pass");

    UmicomKernelUserTaskDestroy(&umicomUserTaskA);
    UmicomKernelUserTaskDestroy(&umicomUserTaskB);
    UmicomKernelPhysicalMemorySnapshot final;
    UmicomKernelUserRequire(UmicomKernelPhysicalMemorySnapshotRead(&final) ==
        UMICOM_KERNEL_MEMORY_OK && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "user-final-allocator-validation");
    UmicomKernelUserRequire(final.allocatedFrames == baseline.allocatedFrames &&
        final.reservedFrames == baseline.reservedFrames && final.freeFrames == baseline.freeFrames
        ? UMICOM_TRUE : UMICOM_FALSE, "user-frame-leak");
    UmicomKernelConsoleWriteLine("user.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("user.completed-cases=12");
    UmicomKernelConsoleWriteLine("user-execution-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_USER_EXECUTION_READY");
}
