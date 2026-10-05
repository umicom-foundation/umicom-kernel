/*-----------------------------------------------------------------------------
 * Umicom Kernel user scheduling native tests
 * File: tests/user_scheduling/user_scheduling_tests.c
 *
 * PURPOSE:
 *   Compile the real scheduler, slice admission, timer policy, C syscall
 *   dispatcher, ELF loader and memory ownership on the host. Only privileged
 *   instructions and the platform clock are replaced with an explicit model.
 *
 * EDUCATIONAL NOTE:
 *   These tests do not prove RISC-V register restoration. The guest program's
 *   Assembly sentinel loop is the acceptance test for that hardware boundary.
 *   Here we can force refusal, early timer, malformed frame and restoration
 *   outcomes deterministically without relying on a host's interrupt timing.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/riscv64/user_slice.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/platform.h"

#define UMICOM_MODEL_RAM_BYTES 1048576U
alignas(UMICOM_MODEL_RAM_BYTES) static UmicomU8 umicomModelRam[UMICOM_MODEL_RAM_BYTES];
static UmicomU8 umicomModelElf[16384U];
static UmicomKernelUserScheduler umicomModelScheduler;
static UmicomRiscvSupervisorMachineState umicomModelMachine;
static UmicomU64 umicomModelNow = 100U;
static UmicomU64 umicomModelCompare = ~(UmicomU64)0U;
static UmicomU64 umicomModelMachineCounter = 7U;
static UmicomU64 umicomModelSupervisorCounter = 3U;
static UmicomU64 umicomModelHart;
static UmicomBoolean umicomModelAllowed = UMICOM_TRUE;
static unsigned umicomModelChecks;
static unsigned umicomModelEntries;
static unsigned umicomModelFences;
static unsigned umicomModelMode;
static unsigned umicomModelPeerMode;
static int umicomModelCheckReentry;
static int umicomModelIgnoreRestore;
static UmicomKernelUserTaskHandle umicomModelReentryHandle;

enum {
    UMICOM_MODEL_TIMER, UMICOM_MODEL_EXIT, UMICOM_MODEL_FAULT,
    UMICOM_MODEL_CALL_TIMER, UMICOM_MODEL_REFUSE, UMICOM_MODEL_EARLY_TIMER,
    UMICOM_MODEL_TIMER_STORM, UMICOM_MODEL_BAD_PRIVILEGE,
    UMICOM_MODEL_CORRUPT_MACHINE, UMICOM_MODEL_CORRUPT_COUNTER,
    UMICOM_MODEL_CORRUPT_COMPARE, UMICOM_MODEL_BAD_TIMER_PC
};
static void UmicomModelRequire(int condition, const char *why)
{
    ++umicomModelChecks;
    if (!condition) { fprintf(stderr, "FAILED: %s\n", why); exit(EXIT_FAILURE); }
}
UmicomBoolean UmicomKernelInterruptContextSwitchAllowed(void) { return umicomModelAllowed; }
UmicomU64 UmicomRiscvReadHartId(void) { return umicomModelHart; }
UmicomU64 UmicomPlatformTimerRead(void) { return umicomModelNow; }
UmicomU64 UmicomPlatformTimerCompareRead(UmicomU64 hart) { (void)hart; return umicomModelCompare; }
void UmicomPlatformTimerSetCompare(UmicomU64 hart, UmicomU64 deadline)
{
    (void)hart;
    if (!umicomModelIgnoreRestore || deadline != ~(UmicomU64)0U) umicomModelCompare = deadline;
}
void UmicomPlatformPhysicalMemoryDescribe(UmicomPlatformPhysicalMemoryInfo *out)
{
    out->base = (UmicomAddress)umicomModelRam;
    out->bytes = sizeof(umicomModelRam);
}
void UmicomRiscvSupervisorMachineStateRead(UmicomRiscvSupervisorMachineState *out) { *out = umicomModelMachine; }
void UmicomRiscvUserCounterStateRead(UmicomU64 *machine, UmicomU64 *supervisor)
{
    *machine = umicomModelMachineCounter; *supervisor = umicomModelSupervisorCounter;
}
void UmicomRiscvExecutableSynchronize(void) { ++umicomModelFences; }

UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    ++umicomModelEntries;
    UmicomModelRequire(request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress &&
        request->entryVirtualAddress == frame->mepc && request->stackTopVirtualAddress == frame->x2_sp,
        "architecture receives this task's retained root, PC and stack");
    const unsigned mode = session->identity == 2U && umicomModelPeerMode != 0U ? umicomModelPeerMode : umicomModelMode;
    if (mode == UMICOM_MODEL_REFUSE) return 1U; /* No register value is consumed on refusal. */
    if (umicomModelCheckReentry) {
        UmicomKernelUserTaskHandle result = 0U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &result)
            == UMICOM_USER_SCHEDULE_BUSY, "nested dispatch is refused");
        UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, umicomModelReentryHandle)
            == UMICOM_USER_SCHEDULE_BUSY, "active task cannot be cancelled");
    }
    /* A real private trap captures these observations. This native model only
     * supplies values to the actual C dispatcher and checks its decisions. */
    UmicomRiscvTrapFrame live = *frame;
    live.mstatus = (UmicomU64)2U << 32U;
    live.reserved = 0U;
    if (mode == UMICOM_MODEL_CALL_TIMER) {
        live.mcause = 8U;
        live.x17_a7 = UMICOM_USER_CALL_IDENTITY;
        if (UmicomKernelUserTrapDispatch(session, &live) == 0U) { *frame = live; return 0U; }
    }
    if (mode == UMICOM_MODEL_EXIT) {
        live.mcause = 8U;
        live.x17_a7 = UMICOM_USER_CALL_EXIT;
        live.x10_a0 = session->identity + 73U;
    } else if (mode == UMICOM_MODEL_FAULT) {
        live.mcause = 15U;
        live.mtval = 0x500000U;
    } else {
        live.mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
        if (mode == UMICOM_MODEL_EARLY_TIMER || mode == UMICOM_MODEL_TIMER_STORM) {
            const unsigned attempts = mode == UMICOM_MODEL_TIMER_STORM ? 17U : 1U;
            for (unsigned index = 0U; index < attempts; ++index) {
                const UmicomRiscvTrapFrame before = live;
                const UmicomU64 resumed = UmicomKernelUserTrapDispatch(session, &live);
                UmicomModelRequire(memcmp(&before, &live, sizeof(live)) == 0, "early timer preserves every saved byte");
                if (index == 16U) { UmicomModelRequire(resumed == 0U, "storm stops at bound"); *frame = live; return 0U; }
                UmicomModelRequire(resumed == 1U, "early timer does not spend a quantum");
            }
        }
        umicomModelNow = umicomModelCompare;
        if (mode == UMICOM_MODEL_BAD_TIMER_PC) live.mepc = 0U;
    }
    if (mode == UMICOM_MODEL_BAD_PRIVILEGE) live.mstatus |= (UmicomU64)3U << 11U;
    UmicomModelRequire(UmicomKernelUserTrapDispatch(session, &live) == 0U, "capture returns to dispatcher");
    *frame = live;
    if (mode == UMICOM_MODEL_CORRUPT_MACHINE) umicomModelMachine.mtvec ^= 16U;
    if (mode == UMICOM_MODEL_CORRUPT_COUNTER) umicomModelMachineCounter ^= 1U;
    if (mode == UMICOM_MODEL_CORRUPT_COMPARE) umicomModelIgnoreRestore = 1;
    return 0U;
}

static void UmicomModelPut(UmicomSize at, UmicomU64 value, UmicomSize width)
{
    for (UmicomSize index = 0U; index < width; ++index) umicomModelElf[at + index] = (UmicomU8)(value >> (index * 8U));
}
static void UmicomModelSegment(UmicomSize index, UmicomU64 flags, UmicomU64 offset,
    UmicomU64 address, UmicomU64 bytes)
{
    const UmicomSize at = 64U + index * 56U;
    UmicomModelPut(at, 1U, 4U); UmicomModelPut(at + 4U, flags, 4U);
    UmicomModelPut(at + 8U, offset, 8U); UmicomModelPut(at + 16U, address, 8U);
    UmicomModelPut(at + 32U, bytes, 8U); UmicomModelPut(at + 40U, bytes, 8U);
    UmicomModelPut(at + 48U, 4096U, 8U);
}
static void UmicomModelSetup(void)
{
    memset(umicomModelRam, 0, sizeof(umicomModelRam));
    UmicomModelRequire(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomModelRam,
        sizeof(umicomModelRam)) == UMICOM_KERNEL_MEMORY_OK, "initialise model RAM");
    memset(&umicomModelScheduler, 0, sizeof(umicomModelScheduler));
    memset(umicomModelElf, 0, sizeof(umicomModelElf));
    umicomModelElf[0] = 0x7fU; umicomModelElf[1] = 'E'; umicomModelElf[2] = 'L'; umicomModelElf[3] = 'F';
    umicomModelElf[4] = 2U; umicomModelElf[5] = 1U; umicomModelElf[6] = 1U;
    UmicomModelPut(16U, 2U, 2U); UmicomModelPut(18U, 243U, 2U); UmicomModelPut(20U, 1U, 4U);
    UmicomModelPut(24U, 0x400000U, 8U); UmicomModelPut(32U, 64U, 8U);
    UmicomModelPut(48U, 1U, 4U); UmicomModelPut(52U, 64U, 2U);
    UmicomModelPut(54U, 56U, 2U); UmicomModelPut(56U, 3U, 2U);
    UmicomModelSegment(0U, 5U, 0x1000U, 0x400000U, 1024U);
    UmicomModelSegment(1U, 4U, 0x2000U, 0x500000U, 16U);
    UmicomModelSegment(2U, 6U, 0x3000U, 0x600000U, 64U);
    for (UmicomSize at = 0x1000U; at < 0x1400U; at += 4U) umicomModelElf[at] = 0x73U;
    memset(&umicomModelMachine, 0, sizeof(umicomModelMachine));
    umicomModelMachine.mstatus = (UmicomU64)10U << 32U;
    umicomModelMachine.mtvec = 0x80200000U;
    umicomModelMachine.mscratch = 0x80300000U;
    UmicomModelRequire(UmicomKernelUserSchedulerInitialize(&umicomModelScheduler) == UMICOM_USER_SCHEDULE_OK,
        "initialise scheduler");
}
static UmicomKernelUserTaskHandle UmicomModelCreate(UmicomU64 budget)
{
    UmicomKernelUserTaskHandle handle = 0U;
    UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf,
        sizeof(umicomModelElf), 17U, budget, &handle) == UMICOM_USER_SCHEDULE_OK, "create owned task");
    return handle;
}
static UmicomKernelUserTask *UmicomModelRecord(UmicomKernelUserTaskHandle handle)
{
    return &umicomModelScheduler.tasks[(UmicomU32)handle - 1U];
}
static UmicomKernelUserTaskInfo UmicomModelInfo(UmicomKernelUserTaskHandle handle)
{
    UmicomKernelUserTaskInfo info;
    UmicomModelRequire(UmicomKernelUserTaskQuery(&umicomModelScheduler, handle, &info) == UMICOM_USER_SCHEDULE_OK,
        "query value snapshot");
    return info;
}
static UmicomKernelUserTaskHandle UmicomModelRun(void)
{
    UmicomKernelUserTaskHandle selected = 0U;
    UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected)
        == UMICOM_USER_SCHEDULE_OK, "dispatch one captured slice");
    return selected;
}
static void UmicomModelCancelReap(UmicomKernelUserTaskHandle handle)
{
    const UmicomKernelUserTaskInfo info = UmicomModelInfo(handle);
    if (info.state == UMICOM_USER_TASK_READY || info.state == UMICOM_USER_TASK_PAUSED)
        UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, handle) == UMICOM_USER_SCHEDULE_OK,
            "cancel retained continuation");
    UmicomModelRequire(UmicomKernelUserTaskReap(&umicomModelScheduler, handle) == UMICOM_USER_SCHEDULE_OK,
        "reap terminal image");
}
static UmicomKernelPhysicalMemorySnapshot UmicomModelMemory(void)
{
    UmicomKernelPhysicalMemorySnapshot result;
    UmicomModelRequire(UmicomKernelPhysicalMemorySnapshotRead(&result) == UMICOM_KERNEL_MEMORY_OK, "read accounting");
    return result;
}
static void UmicomModelNoLeaks(void)
{
    UmicomModelRequire(UmicomModelMemory().allocatedFrames == 0U &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "all physical allocations returned");
}

int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    const char *const name = argv[1];
    UmicomModelSetup();
    if (!strcmp(name, "null-initialise")) {
        UmicomModelRequire(UmicomKernelUserSchedulerInitialize(NULL) == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT, "null init refused");
    } else if (!strcmp(name, "reinitialise")) {
        UmicomModelRequire(UmicomKernelUserSchedulerInitialize(&umicomModelScheduler) == UMICOM_USER_SCHEDULE_BAD_STATE, "no reset");
    } else if (!strcmp(name, "copied-owner")) {
        UmicomKernelUserScheduler *copy = malloc(sizeof(*copy));
        UmicomModelRequire(copy != NULL, "allocate test copy"); *copy = umicomModelScheduler;
        UmicomModelRequire(UmicomKernelUserSchedulerValidate(copy) == UMICOM_USER_SCHEDULE_BAD_STATE, "copy rejected"); free(copy);
    } else if (!strcmp(name, "malformed-admission")) {
        umicomModelElf[0] = 0U; UmicomKernelUserTaskHandle result = 17U;
        UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf, sizeof(umicomModelElf), 0U, 10U, &result)
            == UMICOM_USER_SCHEDULE_LOAD_FAILED && result == 17U, "no success handle on failed image"); UmicomModelNoLeaks();
    } else if (!strcmp(name, "invalid-admission-budget")) {
        UmicomKernelUserTaskHandle result = 17U;
        UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf, sizeof(umicomModelElf), 0U, 0U, &result)
            == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT && result == 17U, "zero budget refused");
        UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf, sizeof(umicomModelElf), 0U,
            UMICOM_USER_TASK_SLICE_LIMIT + 1U, &result) == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT, "excess budget refused");
    } else if (!strcmp(name, "capacity")) {
        UmicomKernelUserTaskHandle handles[UMICOM_USER_TASK_LIMIT];
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) handles[i] = UmicomModelCreate(10U);
        const UmicomU64 before = UmicomModelMemory().allocatedFrames; UmicomKernelUserTaskHandle extra = 31U;
        UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf, sizeof(umicomModelElf), 0U, 10U, &extra)
            == UMICOM_USER_SCHEDULE_CAPACITY && extra == 31U && UmicomModelMemory().allocatedFrames == before, "capacity has no allocation");
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
            UmicomModelCancelReap(handles[i]);
        }
        UmicomModelNoLeaks();
    } else if (!strcmp(name, "identity-exhaustion")) {
        umicomModelScheduler.nextIdentity = ~(UmicomU64)0U;
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
        UmicomModelRequire(UmicomModelInfo(handle).identity == ~(UmicomU64)0U, "last identity once");
        UmicomKernelUserTaskHandle extra = 0U;
        UmicomModelRequire(UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf, sizeof(umicomModelElf), 0U, 10U, &extra)
            == UMICOM_USER_SCHEDULE_IDENTITY_EXHAUSTED, "identity does not wrap"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "idle")) {
        UmicomKernelUserTaskHandle result = 55U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &result) == UMICOM_USER_SCHEDULE_IDLE && result == 55U,
            "idle does not invent a selected handle");
    } else if (!strcmp(name, "invalid-quantum")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomKernelUserTaskHandle selected = 99U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 0U, &selected) == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT &&
            selected == 99U && umicomModelEntries == 0U, "zero quantum untouched");
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, UMICOM_USER_QUANTUM_MAX_TICKS + 1U, &selected)
            == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT, "excess quantum refused"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "round-robin")) {
        const UmicomKernelUserTaskHandle a = UmicomModelCreate(10U), b = UmicomModelCreate(10U);
        for (unsigned i = 0U; i < 4U; ++i) { UmicomModelRequire(UmicomModelRun() == a, "A selected"); UmicomModelRequire(UmicomModelRun() == b, "B selected"); }
        UmicomModelRequire(UmicomModelInfo(a).preemptions == 4U && UmicomModelInfo(b).preemptions == 4U, "both progressed");
        UmicomModelCancelReap(a); UmicomModelCancelReap(b); UmicomModelNoLeaks();
    } else if (!strcmp(name, "all-integer-state")) {
        const UmicomKernelUserTaskHandle a = UmicomModelCreate(10U), b = UmicomModelCreate(10U);
        UmicomU64 valuesA[31], valuesB[31];
        for (unsigned i = 0U; i < 31U; ++i) { valuesA[i] = 0x8000U + i; valuesB[i] = 0x9000U + i; }
        memcpy(&UmicomModelRecord(a)->frame, valuesA, sizeof(valuesA));
        memcpy(&UmicomModelRecord(b)->frame, valuesB, sizeof(valuesB));
        (void)UmicomModelRun(); (void)UmicomModelRun(); (void)UmicomModelRun(); (void)UmicomModelRun();
        UmicomModelRequire(memcmp(valuesA, &UmicomModelRecord(a)->frame, sizeof(valuesA)) == 0 &&
            memcmp(valuesB, &UmicomModelRecord(b)->frame, sizeof(valuesB)) == 0, "saved register arrays are not restarted or shared");
        UmicomModelCancelReap(a); UmicomModelCancelReap(b);
    } else if (!strcmp(name, "interrupted-pc")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomModelRecord(handle)->frame.mepc += 6U;
        (void)UmicomModelRun(); (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(handle).resumePc == 0x400006U, "timer does not add instruction length"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "syscall-then-pause")) {
        umicomModelMode = UMICOM_MODEL_CALL_TIMER; const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
        (void)UmicomModelRun(); (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(handle).resumePc == 0x400008U && UmicomModelInfo(handle).systemCalls == 2U &&
            UmicomModelRecord(handle)->frame.x10_a0 == UmicomModelInfo(handle).identity, "only ECALL advances the PC"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "address-space-separation")) {
        const UmicomKernelUserTaskHandle a = UmicomModelCreate(10U), b = UmicomModelCreate(10U); const UmicomU8 byte = 79U; UmicomU8 other = 9U;
        UmicomModelRequire(UmicomKernelUserMemoryWrite(&UmicomModelRecord(a)->process.report.memory, 0x600000U, &byte, 1U) == 0U,
            "write A's private byte");
        UmicomModelRequire(UmicomKernelUserMemoryRead(&UmicomModelRecord(b)->process.report.memory, 0x600000U, &other, 1U) == 0U && other == 0U,
            "same virtual address in B is independent"); UmicomModelCancelReap(a); UmicomModelCancelReap(b);
    } else if (!strcmp(name, "paused-reap-refused")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); (void)UmicomModelRun();
        const UmicomU64 pages = UmicomModelMemory().allocatedFrames;
        UmicomModelRequire(UmicomKernelUserTaskReap(&umicomModelScheduler, handle) == UMICOM_USER_SCHEDULE_BAD_STATE &&
            UmicomModelMemory().allocatedFrames == pages, "retained continuation pins image"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "lower-destructor-refused")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); (void)UmicomModelRun();
        UmicomModelRequire(UmicomKernelProcessDestroy(&UmicomModelRecord(handle)->process) == UMICOM_PROCESS_BAD_STATE,
            "old destructor cannot bypass continuation owner"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "cancel-unstarted") || !strcmp(name, "cancel-paused")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
        if (!strcmp(name, "cancel-paused")) (void)UmicomModelRun();
        UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, handle) == UMICOM_USER_SCHEDULE_OK, "cancel continuation");
        UmicomKernelUserTaskHandle selected = 0U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected) == UMICOM_USER_SCHEDULE_IDLE,
            "cancelled task is not selected"); UmicomModelCancelReap(handle); UmicomModelNoLeaks();
    } else if (!strcmp(name, "stale-handle-reuse")) {
        const UmicomKernelUserTaskHandle old = UmicomModelCreate(10U); UmicomModelCancelReap(old);
        const UmicomKernelUserTaskHandle fresh = UmicomModelCreate(10U); UmicomKernelUserTaskInfo info;
        UmicomModelRequire(old != fresh && UmicomKernelUserTaskQuery(&umicomModelScheduler, old, &info) == UMICOM_USER_SCHEDULE_INVALID_HANDLE,
            "closed generation never selects replacement"); UmicomModelCancelReap(fresh);
    } else if (!strcmp(name, "forged-token")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomKernelUserTaskInfo info;
        UmicomModelRequire(UmicomKernelUserTaskQuery(&umicomModelScheduler, handle ^ ((UmicomU64)1U << 32U), &info)
            == UMICOM_USER_SCHEDULE_INVALID_HANDLE, "wrong generation refused");
        UmicomModelRequire(UmicomKernelUserTaskQuery(&umicomModelScheduler, ~(UmicomU64)0U, &info)
            == UMICOM_USER_SCHEDULE_INVALID_HANDLE, "index checked before access"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "generation-exhaustion")) {
        umicomModelScheduler.tasks[0].generation = ~(UmicomU32)0U - 1U;
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomModelCancelReap(handle);
        UmicomModelRequire(umicomModelScheduler.tasks[0].retired != UMICOM_FALSE, "last generation retires");
        const UmicomKernelUserTaskHandle next = UmicomModelCreate(10U);
        UmicomModelRequire((UmicomU32)next == 2U, "different slot rather than wrapped generation"); UmicomModelCancelReap(next);
    } else if (!strcmp(name, "quantum-budget")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(3U);
        for (unsigned i = 0U; i < 3U; ++i) (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(handle).state == UMICOM_USER_TASK_EXHAUSTED && UmicomModelInfo(handle).preemptions == 3U,
            "busy program stops after budget"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "fault-and-survivor")) {
        umicomModelMode = UMICOM_MODEL_FAULT; umicomModelPeerMode = UMICOM_MODEL_CALL_TIMER;
        const UmicomKernelUserTaskHandle a = UmicomModelCreate(10U), b = UmicomModelCreate(10U);
        (void)UmicomModelRun(); (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(a).state == UMICOM_USER_TASK_FAULTED && UmicomModelInfo(b).state == UMICOM_USER_TASK_PAUSED,
            "fault does not terminate sibling"); UmicomModelCancelReap(a); UmicomModelCancelReap(b);
    } else if (!strcmp(name, "exit-is-terminal")) {
        umicomModelMode = UMICOM_MODEL_EXIT; const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); (void)UmicomModelRun();
        UmicomKernelUserTaskHandle selected = 0U;
        UmicomModelRequire(UmicomModelInfo(handle).exitValue == 74U && UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected)
            == UMICOM_USER_SCHEDULE_IDLE, "no restart after exit"); UmicomModelCancelReap(handle); UmicomModelNoLeaks();
    } else if (!strcmp(name, "cumulative-call-budget")) {
        umicomModelMode = UMICOM_MODEL_CALL_TIMER; const UmicomKernelUserTaskHandle handle = UmicomModelCreate(128U);
        for (unsigned i = 0U; i <= UMICOM_USER_CALL_LIMIT; ++i) (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(handle).systemCalls == UMICOM_USER_CALL_LIMIT + 1U &&
            UmicomModelRecord(handle)->process.report.stopReason == UMICOM_USER_STOP_CALL_BUDGET,
            "call budget survives successive timer stops"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "ownership-refusal") || !strcmp(name, "timer-in-use") || !strcmp(name, "clock-overflow") ||
        !strcmp(name, "wrong-hart") || !strcmp(name, "pmp-policy") || !strcmp(name, "live-mprv") || !strcmp(name, "live-fpu") ||
        !strcmp(name, "hardware-refusal")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomRiscvTrapFrame original = UmicomModelRecord(handle)->frame;
        if (!strcmp(name, "ownership-refusal")) umicomModelAllowed = UMICOM_FALSE;
        if (!strcmp(name, "timer-in-use")) umicomModelCompare = 900U;
        if (!strcmp(name, "clock-overflow")) umicomModelNow = ~(UmicomU64)0U - 500U;
        if (!strcmp(name, "wrong-hart")) umicomModelHart = 1U;
        if (!strcmp(name, "pmp-policy")) umicomModelMachine.pmpcfg0 = 31U;
        if (!strcmp(name, "live-mprv")) umicomModelMachine.mstatus |= 0x20000U;
        if (!strcmp(name, "live-fpu")) umicomModelMachine.mstatus |= 0x2000U;
        if (!strcmp(name, "hardware-refusal")) umicomModelMode = UMICOM_MODEL_REFUSE;
        UmicomKernelUserTaskHandle selected = 77U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected) == UMICOM_USER_SCHEDULE_ENTRY_REFUSED &&
            selected == 77U && UmicomModelInfo(handle).state == UMICOM_USER_TASK_READY && UmicomModelInfo(handle).slices == 0U &&
            memcmp(&original, &UmicomModelRecord(handle)->frame, sizeof(original)) == 0, "refusal preserves output, registers and budget");
        UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "privileged-frame") || !strcmp(name, "odd-pc") || !strcmp(name, "unmapped-pc") ||
        !strcmp(name, "reserved-word") || !strcmp(name, "floating-frame") || !strcmp(name, "wrong-xlen")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomRiscvTrapFrame *f = &UmicomModelRecord(handle)->frame;
        if (!strcmp(name, "privileged-frame")) f->mstatus |= 0x1800U;
        if (!strcmp(name, "floating-frame")) f->mstatus |= 0x6000U;
        if (!strcmp(name, "odd-pc")) f->mepc |= 1U;
        if (!strcmp(name, "unmapped-pc")) f->mepc = 0U;
        if (!strcmp(name, "reserved-word")) f->reserved = 1U;
        if (!strcmp(name, "wrong-xlen")) f->mstatus = (UmicomU64)1U << 32U;
        UmicomKernelUserTaskHandle selected = 0U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected) == UMICOM_USER_SCHEDULE_INVALID_CONTEXT &&
            umicomModelEntries == 0U, "invalid frame never enters architecture"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "zero-user-stack")) {
        umicomModelMode = UMICOM_MODEL_EXIT; const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
        UmicomModelRecord(handle)->frame.x2_sp = 0U; (void)UmicomModelRun();
        UmicomModelRequire(UmicomModelInfo(handle).state == UMICOM_USER_TASK_EXITED && UmicomModelInfo(handle).savedStack == 0U,
            "EXIT needs no user-stack dereference"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "early-timer") || !strcmp(name, "timer-storm")) {
        umicomModelMode = !strcmp(name, "early-timer") ? UMICOM_MODEL_EARLY_TIMER : UMICOM_MODEL_TIMER_STORM;
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); (void)UmicomModelRun();
        UmicomModelRequire(umicomModelMode == UMICOM_MODEL_EARLY_TIMER ? UmicomModelInfo(handle).preemptions == 1U :
            UmicomModelInfo(handle).state == UMICOM_USER_TASK_ERROR, "early interrupt policy and finite storm handling");
        UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "wrong-session-binding") || !strcmp(name, "recursive-binding")) {
        const UmicomKernelUserTaskHandle a = UmicomModelCreate(10U), b = UmicomModelCreate(10U);
        UmicomKernelUserSession *const as = &UmicomModelRecord(a)->process.report, *const bs = &UmicomModelRecord(b)->process.report;
        UmicomModelRequire(UmicomKernelUserSliceBegin(as, 200U) == UMICOM_TRUE, "bind A");
        if (!strcmp(name, "recursive-binding")) UmicomModelRequire(UmicomKernelUserSliceBegin(bs, 200U) == UMICOM_FALSE, "do not overwrite A");
        else {
            UmicomRiscvTrapFrame frame = UmicomModelRecord(b)->frame; frame.mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
            UmicomModelRequire(UmicomKernelUserTrapDispatch(bs, &frame) == 0U && bs->stopReason == UMICOM_USER_STOP_DEADLINE,
                "unbound session keeps run-once terminal deadline");
        }
        UmicomBoolean expired = UMICOM_FALSE;
        UmicomModelRequire(UmicomKernelUserSliceEnd(bs, &expired) == UMICOM_FALSE && UmicomKernelUserSliceEnd(as, &expired) == UMICOM_TRUE,
            "only matching session detaches"); UmicomModelCancelReap(a); UmicomModelCancelReap(b);
    } else if (!strcmp(name, "corrupt-control-state") || !strcmp(name, "corrupt-counter-policy") ||
        !strcmp(name, "compare-not-restored") || !strcmp(name, "wrong-trap-privilege")) {
        const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
        if (!strcmp(name, "corrupt-control-state")) umicomModelMode = UMICOM_MODEL_CORRUPT_MACHINE;
        if (!strcmp(name, "corrupt-counter-policy")) umicomModelMode = UMICOM_MODEL_CORRUPT_COUNTER;
        if (!strcmp(name, "compare-not-restored")) umicomModelMode = UMICOM_MODEL_CORRUPT_COMPARE;
        if (!strcmp(name, "wrong-trap-privilege")) umicomModelMode = UMICOM_MODEL_BAD_PRIVILEGE;
        UmicomKernelUserTaskHandle result = 0U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &result) == UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR,
            "unverified return is not a user fault");
        UmicomModelRequire(UmicomModelInfo(handle).state == UMICOM_USER_TASK_ERROR && UmicomKernelUserTaskReap(&umicomModelScheduler, handle)
            == UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR && UmicomModelMemory().allocatedFrames != 0U,
            "unsafe image retained for diagnosis, not freed");
    } else if (!strcmp(name, "invalid-timer-pc")) {
        umicomModelMode = UMICOM_MODEL_BAD_TIMER_PC; const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U); UmicomKernelUserTaskHandle result;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &result) == UMICOM_USER_SCHEDULE_INVALID_CONTEXT,
            "timer cannot make an invalid PC resumable"); UmicomModelCancelReap(handle);
    } else if (!strcmp(name, "reentry")) {
        umicomModelCheckReentry = 1; umicomModelReentryHandle = UmicomModelCreate(10U); (void)UmicomModelRun();
        UmicomModelCancelReap(umicomModelReentryHandle);
    } else if (!strcmp(name, "empty-retained-reap")) {
        UmicomSize count = 77U; UmicomModelRequire(UmicomKernelUserSchedulerReapRetained(&umicomModelScheduler, &count)
            == UMICOM_USER_SCHEDULE_OK && count == 0U, "no invented rollback records");
    } else if (!strcmp(name, "allocation-rollback")) {
        for (UmicomSize freePages = 0U; freePages <= 24U; ++freePages) {
            UmicomModelSetup();
            UmicomModelRequire(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)umicomModelRam + freePages * 4096U,
                sizeof(umicomModelRam) - freePages * 4096U) == UMICOM_KERNEL_MEMORY_OK, "reserve controlled budget");
            UmicomKernelUserTaskHandle handle = 55U;
            const UmicomKernelUserScheduleStatus status = UmicomKernelUserTaskCreate(&umicomModelScheduler, umicomModelElf,
                sizeof(umicomModelElf), 0U, 10U, &handle);
            if (status == UMICOM_USER_SCHEDULE_OK) UmicomModelCancelReap(handle);
            else UmicomModelRequire(status == UMICOM_USER_SCHEDULE_LOAD_FAILED && handle == 55U, "rollback publishes no handle");
            UmicomModelNoLeaks();
        }
    } else if (!strcmp(name, "repeated-lifetimes")) {
        UmicomKernelUserTaskHandle old = 0U;
        for (unsigned i = 0U; i < 1000U; ++i) {
            const UmicomKernelUserTaskHandle handle = UmicomModelCreate(10U);
            UmicomModelRequire(handle != old, "generation changes each lifetime"); old = handle;
            (void)UmicomModelRun(); UmicomModelCancelReap(handle);
        }
        UmicomModelNoLeaks();
    } else { fprintf(stderr, "Unknown native case: %s\n", name); return EXIT_FAILURE; }
    printf("PASS %s (%u checks)\n", name, umicomModelChecks);
    return EXIT_SUCCESS;
}
