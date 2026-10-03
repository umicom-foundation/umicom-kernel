/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/supervisor_payload.c
 *
 * PURPOSE:
 *   Run a small, ordinary C function in supervisor mode using translated
 *   instruction fetch, a private stack and one shared observation page.
 *
 * EDUCATIONAL OVERVIEW:
 *   This function cannot use the normal console or allocator: their code and
 *   machine data are deliberately absent from its page table. Instead, it
 *   leaves observations in a mapped page and returns to an Assembly ECALL.
 *   Machine mode prints the evidence only after it has regained control.
 *
 *   Keeping this payload small makes the privilege change easy to inspect.
 *   Volatile locals force real stack traffic; success cannot come only from
 *   arithmetic that the compiler kept in registers.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* This is the only Kernel contract the isolated payload needs. */
#include "umicom/kernel/riscv64/supervisor.h"

/* The linker gives this section its own complete pages. A normal text wildcard
 * would mix supervisor-visible code with machine-only implementation. */
__attribute__((section(".text.umicom_supervisor_payload"), noinline))
UmicomU64 UmicomRiscvSupervisorPayload(
    volatile UmicomRiscvSupervisorObservation *observation
)
{
    /* These words live on the supervisor stack, not in a global object. Each
     * assignment and read must become an actual translated memory operation. */
    volatile UmicomU64 stackWords[4];

    /* First prove that writing the shared observation mapping is possible. */
    observation->entered = (UmicomU64)1U;

    /* Read satp from S-mode itself. The machine caller will compare this with
     * the exact root it selected, not merely check that MODE is nonzero. */
    observation->observedSatp = UmicomRiscvSupervisorSatpRead();

    /* Keep a stack address while this C frame is live. It must fall inside the
     * dedicated writable, non-executable stack page. */
    observation->observedStack = UmicomRiscvSupervisorStackRead();

    /* Use different small values so swapped or missing stack stores affect the
     * result. The final expression should produce 0x404. */
    stackWords[0] = (UmicomU64)0x101U;
    stackWords[1] = (UmicomU64)0x202U;
    stackWords[2] = (UmicomU64)0x303U;
    stackWords[3] = (UmicomU64)0x404U;

    /* Read back the volatile locals through the live translated stack. */
    observation->stackResult =
        (stackWords[0] + stackWords[1]) ^ (stackWords[2] + stackWords[3]);

    /* The ordinary path skips these branches. Each negative path faults at an
     * explicitly named Assembly instruction, so a different fault is not
     * mistaken for the protection behaviour we intended to test. */
    if (observation->operation == UMICOM_SUPERVISOR_OPERATION_GUARD_LOAD) {
        /* There is no leaf for this address. The MMU must raise a load page
         * fault, even though PMP would allow its potential backing RAM. */
        (void)UmicomRiscvSupervisorGuardLoad(observation->guardVirtualAddress);
    }

    /* A readable leaf is not writable merely because its backing frame exists. */
    if (observation->operation == UMICOM_SUPERVISOR_OPERATION_READONLY_STORE) {
        /* The machine caller later checks the original physical word too. */
        UmicomRiscvSupervisorReadOnlyStore(
            observation->readOnlyVirtualAddress,
            (UmicomU64)0xfeedfaceU
        );
    }

    /* This access distinguishes a real privilege transition from code that
     * merely claims to run in supervisor mode while remaining in M-mode. */
    if (observation->operation == UMICOM_SUPERVISOR_OPERATION_MACHINE_CSR) {
        /* Reading mstatus from S-mode must raise an illegal-instruction trap. */
        (void)UmicomRiscvSupervisorMachineStatusRead();
    }

    /* Only a normal execution reaches this line. A negative test that reaches
     * it will fail the machine caller's expected-cause checks. */
    observation->completed = (UmicomU64)1U;

    /* The Assembly caller supplies the completion cookie and executes ECALL.
     * Returning this value also exercises the normal RV64 C calling convention. */
    return observation->stackResult;
}
