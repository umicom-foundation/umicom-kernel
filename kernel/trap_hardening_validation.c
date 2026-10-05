/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/trap_hardening_validation.c
 *
 * PURPOSE:
 *   Exercise the real machine trap stack, complete integer preservation,
 *   controlled effective-privilege faults and safe continuation afterwards.
 *
 * EDUCATIONAL OVERVIEW:
 *   An ordinary C local may be spilled and fail to reveal a missing register
 *   save. The Assembly probes seed every integer register and deliberately use
 *   sp=0 at the trap boundary. Memory probes then ask the real MMU to refuse
 *   access, not merely ask our software walker whether access should fail.
 *
 *   The nested-fault case runs only in a separate test ELF. Its handler stops
 *   the guest rather than returning from a broken Kernel operation. The normal
 *   Kernel image has no success branch for a nested machine fault.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/trap_integrity.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/mmu.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/virtual_memory.h"

static void UmicomTrapRequire(UmicomBoolean condition, const char *reason)
{
    if (condition != UMICOM_FALSE) {
        return;
    }
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("trap-integrity.reason=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(191U);
    UmicomPlatformHalt();
}

static void UmicomTrapHex(const char *name, UmicomU64 value)
{
    UmicomKernelConsoleWrite(name);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteHex64(value);
    UmicomKernelConsoleWriteLine("");
}

static void UmicomTrapStackEvidence(UmicomU64 cause)
{
    UmicomKernelTrapIntegritySnapshot snapshot;
    UmicomKernelTrapIntegritySnapshotRead(&snapshot);
    const UmicomAddress expected = (UmicomAddress)UmicomRiscvMachineTrapStackTop -
        sizeof(UmicomRiscvTrapFrame);
    UmicomTrapRequire(snapshot.interruptedStack == 0U && snapshot.frameAddress == expected &&
        snapshot.cause == cause ? UMICOM_TRUE : UMICOM_FALSE, "owned-stack-evidence");
    UmicomTrapHex("trap-integrity.interrupted-sp", snapshot.interruptedStack);
    UmicomTrapHex("trap-integrity.frame", snapshot.frameAddress);
}

void UmicomKernelTrapHardeningValidate(void)
{
    UmicomKernelConsoleWriteLine("trap-integrity-test=begin");
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    UmicomTrapRequire(UmicomRiscvReadHartId() == 0U && before.satp == 0U &&
        before.mie == 0U && (before.mstatus & 0x26608U) == 0U &&
        before.pmpcfg0 == 0U && before.pmpaddr0 == 0U &&
        before.mtvec == UmicomRiscvTrapVectorAddress() &&
        before.mscratch == (UmicomAddress)UmicomRiscvMachineTrapStackTop
            ? UMICOM_TRUE : UMICOM_FALSE, "machine-admission");
    const UmicomU64 originalCompare = UmicomPlatformTimerCompareRead(0U);
    UmicomKernelPhysicalMemorySnapshot memoryBefore;
    UmicomKernelPhysicalMemorySnapshot memoryAfter;
    UmicomTrapRequire(UmicomKernelPhysicalMemorySnapshotRead(&memoryBefore) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "physical-snapshot-before");

    UmicomKernelConsoleWriteLine("trap-integrity.case=ecall-with-zero-stack");
    UmicomTrapRequire(UmicomRiscvTrapRegistersProbe() == 1U ? UMICOM_TRUE : UMICOM_FALSE,
        "all-integer-registers");
    UmicomTrapStackEvidence(11U);
    UmicomKernelConsoleWriteLine("trap-integrity.integer-registers=preserved");

    UmicomKernelConsoleWriteLine("trap-integrity.case=timer-with-zero-stack");
    const UmicomU64 now = UmicomPlatformTimerRead();
    UmicomTrapRequire(now <= ~(UmicomU64)0U - 100000U ? UMICOM_TRUE : UMICOM_FALSE, "timer-overflow");
    UmicomPlatformTimerSetCompare(0U, now + 100000U);
    /* Keep MIE clear while setting the source. The Assembly probe enables it
     * only after every register is seeded and its interrupted sp is zero. */
    __asm__ volatile("csrw mie, %0" : : "r"((UmicomU64)0x80U) : "memory");
    UmicomTrapRequire(UmicomRiscvTrapTimerRegistersProbe() == 1U ? UMICOM_TRUE : UMICOM_FALSE,
        "timer-registers");
    UmicomTrapStackEvidence(UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U);
    UmicomPlatformTimerSetCompare(0U, originalCompare);
    UmicomKernelConsoleWriteLine("trap-integrity.timer-registers=preserved");

    /* The address space owns its page tables. This function separately owns
     * the read-only backing frame and frees it only after translation stops. */
    UmicomKernelVirtualAddressSpace space = {0};
    UmicomAddress data = 0U;
    const UmicomAddress readOnly = (UmicomAddress)0x100008000ULL;
    const UmicomAddress unmapped = readOnly + UMICOM_KERNEL_PAGE_SIZE;
    const UmicomU64 seed = 0x1928374655aa55aaULL;
    UmicomTrapRequire(UmicomKernelVirtualAddressSpaceCreate(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "create-probe-root");
    UmicomTrapRequire(UmicomKernelPhysicalMemoryAllocateFrame(&data) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "allocate-probe-frame");
    *(volatile UmicomU64 *)data = seed;
    UmicomTrapRequire(UmicomKernelVirtualMemoryMapPage(&space, readOnly, data,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
            ? UMICOM_TRUE : UMICOM_FALSE, "map-read-only");
    UmicomTrapRequire(UmicomKernelVirtualMemoryValidate(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "validate-probe-root");
    UmicomRiscvTranslationValidationPmpEnable();
    UmicomRiscvSv39Activate(space.rootTablePhysicalAddress);

    UmicomKernelConsoleWriteLine("trap-integrity.case=unmapped-effective-load");
    UmicomTrapRequire(UmicomKernelTrapProbeArm(UMICOM_TRAP_ROUTE_PROBE_LOAD), "arm-load");
    UmicomRiscvTrapLoadProbe(unmapped);
    UmicomTrapRequire(UmicomKernelTrapProbeFinished(), "load-fault-did-not-occur");
    UmicomKernelConsoleWriteLine("trap-integrity.load-fault=contained");

    UmicomKernelConsoleWriteLine("trap-integrity.case=read-only-effective-store");
    UmicomTrapRequire(UmicomKernelTrapProbeArm(UMICOM_TRAP_ROUTE_PROBE_STORE), "arm-store");
    UmicomRiscvTrapStoreProbe(readOnly, ~seed);
    UmicomTrapRequire(UmicomKernelTrapProbeFinished(), "store-fault-did-not-occur");
    UmicomTrapRequire(*(volatile UmicomU64 *)data == seed ? UMICOM_TRUE : UMICOM_FALSE,
        "read-only-data-changed");
    UmicomKernelConsoleWriteLine("trap-integrity.store-fault=contained");

    UmicomRiscvAddressTranslationDisable();
    UmicomRiscvTranslationValidationPmpDisable();
    /* PMP writes also affect cached permissions. No probe mapping is used
     * again, and a full fence precedes reclaiming its page-table frames. */
    __asm__ volatile("sfence.vma zero, zero" : : : "memory");
    UmicomTrapRequire(UmicomKernelVirtualMemoryUnmapPage(&space, readOnly) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "unmap-probe-page");
    UmicomTrapRequire(UmicomKernelVirtualAddressSpaceDestroy(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "destroy-probe-root");
    *(volatile UmicomU64 *)data = 0U;
    UmicomTrapRequire(UmicomKernelPhysicalMemoryFreeFrame(data) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "free-probe-page");

    UmicomKernelConsoleWriteLine("trap-integrity.case=continuation-after-faults");
    UmicomTrapRequire(UmicomRiscvTrapRegistersProbe() == 1U ? UMICOM_TRUE : UMICOM_FALSE,
        "post-fault-ecall");
    UmicomRiscvSupervisorMachineStateRead(&after);
    /* MRET sets MPIE and resets MPP as specified by the architecture. Trap
     * diagnostic CSRs retain their latest event; they are not caller state.
     * All other controls and the installed vector/scratch pair must survive. */
    const UmicomU64 returnBits = 0x1880U;
    UmicomTrapRequire((before.mstatus & ~returnBits) == (after.mstatus & ~returnBits) &&
        before.mie == after.mie && before.mtvec == after.mtvec && before.mscratch == after.mscratch &&
        before.satp == after.satp && before.pmpcfg0 == after.pmpcfg0 && before.pmpaddr0 == after.pmpaddr0 &&
        before.medeleg == after.medeleg && before.mideleg == after.mideleg &&
        UmicomPlatformTimerCompareRead(0U) == originalCompare ? UMICOM_TRUE : UMICOM_FALSE,
        "machine-control-state");
    UmicomTrapRequire(UmicomKernelPhysicalMemorySnapshotRead(&memoryAfter) == UMICOM_KERNEL_MEMORY_OK &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK &&
        memoryBefore.allocatedFrames == memoryAfter.allocatedFrames &&
        memoryBefore.reservedFrames == memoryAfter.reservedFrames &&
        memoryBefore.freeFrames == memoryAfter.freeFrames ? UMICOM_TRUE : UMICOM_FALSE,
        "frame-accounting");
    UmicomKernelConsoleWriteLine("trap-integrity.original-policy=preserved");
    UmicomKernelConsoleWriteLine("trap-integrity.control-state=restored");
    UmicomKernelConsoleWriteLine("trap-integrity.frame-accounting=restored");
#ifdef UMICOM_TRAP_NESTED_VALIDATION
    UmicomKernelConsoleWriteLine("trap-integrity.case=nested-machine-fault");
    UmicomRiscvTrapNestedProbe();
    UmicomTrapRequire(UMICOM_FALSE, "nested-fault-returned");
#else
    UmicomKernelConsoleWriteLine("trap-integrity.completed-cases=5");
    UmicomKernelConsoleWriteLine("trap-integrity-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_TRAP_INTEGRITY_READY");
#endif
}
