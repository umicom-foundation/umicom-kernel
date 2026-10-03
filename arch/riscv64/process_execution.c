/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/process_execution.c
 *
 * PURPOSE:
 *   Run a ready, loaded process through the existing RV64 user monitor and
 *   preserve its terminal report without changing the monitor's implementation.
 *
 * EDUCATIONAL OVERVIEW:
 *   Loading is independent of execution. A malformed file must fail before
 *   privilege changes, and an exited program must retain its memory until the
 *   owner chooses to destroy it. This adapter is the narrow connection between
 *   that lifetime model and the privileged user-entry routine already tested.
 *
 *   Only one invocation may run at a time. The busy flag is a single-hart
 *   reentry guard, not an atomic lock or an SMP scheduler. The caller remains
 *   responsible for keeping these Kernel-owned objects private.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"

/* The assembler implements the instruction-cache synchronisation required
 * after data stores created bytes which the same hart will execute. */
void UmicomRiscvExecutableSynchronize(void);
static UmicomBoolean umicomProcessInvocationActive;

static UmicomBoolean UmicomProcessMachineStateEqual(
    const UmicomRiscvSupervisorMachineState *left,
    const UmicomRiscvSupervisorMachineState *right)
{
    /* Compare fields, not structure padding. Every listed CSR is borrowed by
     * the entry code and must return to its precise caller-visible value. */
    return left->mstatus == right->mstatus && left->mie == right->mie &&
        left->mtvec == right->mtvec && left->mscratch == right->mscratch &&
        left->medeleg == right->medeleg && left->mideleg == right->mideleg &&
        left->satp == right->satp && left->pmpcfg0 == right->pmpcfg0 &&
        left->pmpaddr0 == right->pmpaddr0 && left->mepc == right->mepc &&
        left->mcause == right->mcause && left->mtval == right->mtval
        ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomKernelProcessStatus UmicomKernelProcessRun(
    UmicomKernelProcess *process, UmicomU64 argument, UmicomU64 deadlineTicks)
{
    /* Ten million ticks is one second on the selected QEMU timer. This upper
     * bound is admission policy, not a scheduler time slice. */
    if (process == (UmicomKernelProcess *)0 || deadlineTicks == 0U || deadlineTicks > 10000000U) {
        return UMICOM_PROCESS_INVALID_ARGUMENT;
    }
    if (process->state != UMICOM_PROCESS_READY || umicomProcessInvocationActive != UMICOM_FALSE) {
        return UMICOM_PROCESS_BAD_STATE;
    }
    if (UmicomRiscvReadHartId() != 0U) {
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    /* Do not borrow a live interrupt policy or an already translated context.
     * The earlier user-entry routine remains a bounded machine-mode service. */
    if (before.mie != 0U || (before.mstatus & 0x20008U) != 0U ||
        before.satp != 0U || before.pmpcfg0 != 0U) {
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    UmicomPlatformPhysicalMemoryInfo ram;
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    if (ram.bytes < 8U || (ram.bytes & (ram.bytes - 1U)) != 0U ||
        (ram.base & (UmicomAddress)(ram.bytes - 1U)) != 0U) {
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    /* Revalidate the backing view immediately before entry. Only Kernel-owned
     * code can mutate it, and no other hart or mapper runs during this call. */
    if (UmicomKernelVirtualMemoryValidate(&process->space) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK ||
        UmicomKernelUserMemoryCheck(&process->report.memory, process->entry, 4U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != UMICOM_USER_RESULT_OK ||
        UmicomKernelUserMemoryCheck(&process->report.memory, UMICOM_EXECUTABLE_STACK_BASE,
            UMICOM_EXECUTABLE_STACK_PAGES * UMICOM_EXECUTABLE_PAGE_BYTES,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != UMICOM_USER_RESULT_OK) {
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (now > ~(UmicomU64)0U - deadlineTicks) {
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    /* READY can be executed only once. Clear reporting bytes so a previous
     * refused entry cannot accidentally contribute success evidence. */
    volatile UmicomU8 *const reportBytes = (volatile UmicomU8 *)&process->report;
    for (UmicomSize index = 0U; index < sizeof(process->report); ++index) {
        reportBytes[index] = 0U;
    }
    process->report.memory.space = &process->space;
    process->report.memory.pages = process->pages;
    process->report.memory.pageCount = process->pageCount;
    process->report.identity = process->identity;
    UmicomRiscvUserRequest request;
    request.rootTablePhysicalAddress = process->space.rootTablePhysicalAddress;
    request.entryVirtualAddress = process->entry;
    request.stackTopVirtualAddress = UMICOM_EXECUTABLE_STACK_TOP;
    request.argument = argument;
    request.pmpNapotAddress = ((UmicomU64)ram.base >> 2U) | ((ram.bytes >> 3U) - 1U);

    UmicomU64 counterBefore = 0U;
    UmicomU64 supervisorCounterBefore = 0U;
    __asm__ volatile("csrr %0, mcounteren" : "=r"(counterBefore));
    __asm__ volatile("csrr %0, scounteren" : "=r"(supervisorCounterBefore));
    const UmicomU64 oldCompare = UmicomPlatformTimerCompareRead(0U);
    /* This is not cache maintenance for other harts; this monitor runs only
     * on hart zero. A future SMP loader needs a remote synchronisation policy. */
    UmicomRiscvExecutableSynchronize();
    UmicomPlatformTimerSetCompare(0U, now + deadlineTicks);
    umicomProcessInvocationActive = UMICOM_TRUE;
    process->state = UMICOM_PROCESS_RUNNING;
    process->quiesced = UMICOM_FALSE; /* Reclamation is forbidden until state restoration is proved. */
    const UmicomU64 entered = UmicomRiscvUserExecute(&request, &process->report);
    /* Always restore the borrowed deadline before examining the terminal event. */
    UmicomPlatformTimerSetCompare(0U, oldCompare);
    umicomProcessInvocationActive = UMICOM_FALSE;
    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomU64 counterAfter = 0U;
    UmicomU64 supervisorCounterAfter = 0U;
    __asm__ volatile("csrr %0, mcounteren" : "=r"(counterAfter));
    __asm__ volatile("csrr %0, scounteren" : "=r"(supervisorCounterAfter));
    if (UmicomProcessMachineStateEqual(&before, &after) == UMICOM_FALSE ||
        counterBefore != counterAfter || supervisorCounterBefore != supervisorCounterAfter ||
        UmicomPlatformTimerCompareRead(0U) != oldCompare) {
        process->state = UMICOM_PROCESS_MONITOR_ERROR;
        return UMICOM_PROCESS_MACHINE_STATE_ERROR;
    }
    /* Only a verified return to the saved machine context permits reclamation. */
    process->quiesced = UMICOM_TRUE;
    if (entered != 0U) {
        process->state = UMICOM_PROCESS_READY; /* No user instruction was admitted. */
        return UMICOM_PROCESS_ENTRY_REFUSED;
    }
    /* A user fault is a terminal process result, not an internal loader error.
     * Keep every page alive for inspection until the caller destroys the owner. */
    switch (process->report.stopReason) {
        case UMICOM_USER_STOP_EXIT: process->state = UMICOM_PROCESS_EXITED; break;
        case UMICOM_USER_STOP_FAULT: process->state = UMICOM_PROCESS_FAULTED; break;
        case UMICOM_USER_STOP_DEADLINE: process->state = UMICOM_PROCESS_TIMED_OUT; break;
        case UMICOM_USER_STOP_CALL_BUDGET: process->state = UMICOM_PROCESS_CALL_LIMIT; break;
        default:
            process->state = UMICOM_PROCESS_MONITOR_ERROR;
            return UMICOM_PROCESS_MACHINE_STATE_ERROR;
    }
    return UMICOM_PROCESS_OK;
}
