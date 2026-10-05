/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/user_slice.c
 *
 * PURPOSE:
 *   Borrow an idle machine timer for one user quantum, use the shared register
 *   entry, and prove that the ordinary machine environment has been restored.
 *
 * EDUCATIONAL OVERVIEW:
 *   Pre-emption happens only in U-mode. The dispatcher and system calls remain
 *   serial machine-mode code with MIE clear. We first refuse managed sections
 *   or leases, then borrow the timer exactly within this synchronous call.
 *   No ownership API is invoked while a private vector and root are installed.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/user_slice.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/platform.h"

static UmicomBoolean UmicomSliceMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    /* Compare the borrowed values, never structure padding or evolving clocks. */
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval
        ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomKernelUserScheduleStatus UmicomKernelUserSliceRun(UmicomKernelUserTask *task,
    UmicomU64 quantumTicks, UmicomBoolean *outExpired)
{
    if (task == (UmicomKernelUserTask *)0 || outExpired == (UmicomBoolean *)0 ||
        quantumTicks < UMICOM_USER_QUANTUM_MIN_TICKS || quantumTicks > UMICOM_USER_QUANTUM_MAX_TICKS) {
        return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    }
    /* The scheduler marks a record RUNNING only for this call. The lower image
     * is also kept RUNNING across pauses to prevent its run-once/destructor API
     * from accidentally restarting or freeing a retained continuation. */
    if (task->state != UMICOM_USER_TASK_RUNNING || task->process.state != UMICOM_PROCESS_RUNNING) {
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    }
    if (UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE || UmicomRiscvReadHartId() != 0U) {
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
    }
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    if (before.mie != 0U || (before.mstatus & 0x26608U) != 0U ||
        before.satp != 0U || before.pmpcfg0 != 0U) {
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
    }
    if (UmicomKernelVirtualMemoryValidate(&task->process.space) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK ||
        UmicomKernelUserFrameValid(&task->process.report.memory, &task->frame) == UMICOM_FALSE) {
        return UMICOM_USER_SCHEDULE_INVALID_CONTEXT;
    }
    UmicomPlatformPhysicalMemoryInfo ram;
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    if (ram.bytes < 8U || (ram.bytes & (ram.bytes - 1U)) != 0U ||
        (ram.base & (UmicomAddress)(ram.bytes - 1U)) != 0U) {
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
    }
    const UmicomU64 now = UmicomPlatformTimerRead();
    const UmicomU64 previousCompare = UmicomPlatformTimerCompareRead(0U);
    /* Do not take over a disabled source which still has another deadline.
     * Requiring the parked value also makes repeated slice cleanup observable. */
    if (now > ~(UmicomU64)0U - quantumTicks || previousCompare != ~(UmicomU64)0U) {
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
    }
    const UmicomU64 deadline = now + quantumTicks;
    if (UmicomKernelUserSliceBegin(&task->process.report, deadline) == UMICOM_FALSE) {
        return UMICOM_USER_SCHEDULE_BUSY;
    }
    UmicomU64 machineCounterBefore = 0U;
    UmicomU64 supervisorCounterBefore = 0U;
    UmicomRiscvUserCounterStateRead(&machineCounterBefore, &supervisorCounterBefore);
    UmicomRiscvUserRequest request;
    request.rootTablePhysicalAddress = task->process.space.rootTablePhysicalAddress;
    request.entryVirtualAddress = (UmicomAddress)task->frame.mepc;
    request.stackTopVirtualAddress = (UmicomAddress)task->frame.x2_sp;
    request.argument = task->frame.x10_a0;
    request.pmpNapotAddress = ((UmicomU64)ram.base >> 2U) | ((ram.bytes >> 3U) - 1U);

    /* Frame state is retained. Only the per-invocation outcome is reset; syscall
     * and copy budgets must not restart each time a program is pre-empted. */
    task->process.report.stopReason = UMICOM_USER_STOP_NONE;
    UmicomRiscvExecutableSynchronize();
    UmicomPlatformTimerSetCompare(0U, deadline);
    const UmicomU64 entered = UmicomRiscvUserExecuteFrame(&request, &task->process.report, &task->frame);
    UmicomPlatformTimerSetCompare(0U, previousCompare);
    UmicomBoolean expired = UMICOM_FALSE;
    const UmicomBoolean detached = UmicomKernelUserSliceEnd(&task->process.report, &expired);
    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomU64 machineCounterAfter = 0U;
    UmicomU64 supervisorCounterAfter = 0U;
    UmicomRiscvUserCounterStateRead(&machineCounterAfter, &supervisorCounterAfter);
    if (detached == UMICOM_FALSE || UmicomSliceMachineEqual(&before, &after) == UMICOM_FALSE ||
        machineCounterBefore != machineCounterAfter || supervisorCounterBefore != supervisorCounterAfter ||
        UmicomPlatformTimerCompareRead(0U) != previousCompare) {
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    if (entered != 0U) {
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED; /* No saved frame was consumed or replaced. */
    }
    /* Only a real U-mode trap may become a resumable quantum or terminal result. */
    if (((task->frame.mstatus >> 11U) & 3U) != 0U || task->frame.reserved != 0U) {
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    if (expired != UMICOM_FALSE && (task->frame.mcause != (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U) ||
        UmicomKernelUserFrameValid(&task->process.report.memory, &task->frame) == UMICOM_FALSE)) {
        return UMICOM_USER_SCHEDULE_INVALID_CONTEXT;
    }
    *outExpired = expired;
    return UMICOM_USER_SCHEDULE_OK;
}
