/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/supervisor.S
 *
 * PURPOSE:
 *   Enter a small supervisor payload under Sv39, catch its exit or fault, and
 *   restore the machine-mode caller without changing the established trap code.
 *
 * EDUCATIONAL OVERVIEW:
 *   There are two stacks here. The C caller already has a machine stack. The
 *   supervisor payload gets a different, translated stack. Before MRET lowers
 *   privilege, mscratch records the machine context address. The first trap
 *   instruction swaps sp with mscratch, so even an unmapped supervisor stack
 *   cannot prevent the machine handler from collecting an error report.
 *
 *   The temporary trap handler never resumes a faulting supervisor instruction.
 *   It returns to the saved machine caller instead. That makes negative tests
 *   bounded and keeps the normal Kernel trap dispatcher entirely unchanged.
 *   This trusted single-hart experiment is not a general process switch.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Share report/request offsets with C; the header excludes C declarations
 * when the compiler preprocesses this Assembly file. */
#include "umicom/kernel/riscv64/supervisor.h"

    /* Machine-only transition/return code is not mapped into the payload. */
    .section .text.umicom_supervisor_machine, "ax", @progbits
    .balign 16

    /* The fixed context is private to this file. Its size preserves the
     * sixteen-byte stack alignment required when returning to the C caller. */
    .equ UMICOM_SUPERVISOR_CONTEXT_BYTES, 256
    .equ UMICOM_SUPERVISOR_SAVED_RA, 0 /* Caller ra. */
    .equ UMICOM_SUPERVISOR_SAVED_S0, 8 /* Caller s0. */
    .equ UMICOM_SUPERVISOR_SAVED_S1, 16 /* Caller s1. */
    .equ UMICOM_SUPERVISOR_SAVED_S2, 24 /* Caller s2. */
    .equ UMICOM_SUPERVISOR_SAVED_S3, 32 /* Caller s3. */
    .equ UMICOM_SUPERVISOR_SAVED_S4, 40 /* Caller s4. */
    .equ UMICOM_SUPERVISOR_SAVED_S5, 48 /* Caller s5. */
    .equ UMICOM_SUPERVISOR_SAVED_S6, 56 /* Caller s6. */
    .equ UMICOM_SUPERVISOR_SAVED_S7, 64 /* Caller s7. */
    .equ UMICOM_SUPERVISOR_SAVED_S8, 72 /* Caller s8. */
    .equ UMICOM_SUPERVISOR_SAVED_S9, 80 /* Caller s9. */
    .equ UMICOM_SUPERVISOR_SAVED_S10, 88 /* Caller s10. */
    .equ UMICOM_SUPERVISOR_SAVED_S11, 96 /* Caller s11. */
    .equ UMICOM_SUPERVISOR_SAVED_GP, 104 /* Caller gp. */
    .equ UMICOM_SUPERVISOR_SAVED_TP, 112 /* Caller tp. */
    .equ UMICOM_SUPERVISOR_SAVED_MSTATUS, 120 /* Caller mstatus. */
    .equ UMICOM_SUPERVISOR_SAVED_MIE, 128 /* Caller mie. */
    .equ UMICOM_SUPERVISOR_SAVED_MTVEC, 136 /* Caller mtvec. */
    .equ UMICOM_SUPERVISOR_SAVED_MSCRATCH, 144 /* Caller mscratch. */
    .equ UMICOM_SUPERVISOR_SAVED_MEDELEG, 152 /* Caller medeleg. */
    .equ UMICOM_SUPERVISOR_SAVED_MIDELEG, 160 /* Caller mideleg. */
    .equ UMICOM_SUPERVISOR_SAVED_SATP, 168 /* Caller satp. */
    .equ UMICOM_SUPERVISOR_SAVED_PMPCFG0, 176 /* Caller pmpcfg0. */
    .equ UMICOM_SUPERVISOR_SAVED_PMPADDR0, 184 /* Caller pmpaddr0. */
    .equ UMICOM_SUPERVISOR_SAVED_MEPC, 192 /* Caller mepc. */
    .equ UMICOM_SUPERVISOR_SAVED_MCAUSE, 200 /* Caller mcause. */
    .equ UMICOM_SUPERVISOR_SAVED_MTVAL, 208 /* Caller mtval. */

    .equ UMICOM_SUPERVISOR_SAVED_REPORT, 216
    .equ UMICOM_SUPERVISOR_SAVED_REQUEST, 224
    .equ UMICOM_SUPERVISOR_SAVED_RESULT, 232

    .global UmicomRiscvSupervisorExecute
    .type UmicomRiscvSupervisorExecute, @function
UmicomRiscvSupervisorExecute:
    /* Reserve the complete machine context before changing any privilege state.
     * The caller guarantees MPRV is clear and these pointers are Kernel-owned. */
    addi sp, sp, -UMICOM_SUPERVISOR_CONTEXT_BYTES
    sd ra, UMICOM_SUPERVISOR_SAVED_RA(sp) /* Preserve caller ra. */
    sd s0, UMICOM_SUPERVISOR_SAVED_S0(sp) /* Preserve caller s0. */
    sd s1, UMICOM_SUPERVISOR_SAVED_S1(sp) /* Preserve caller s1. */
    sd s2, UMICOM_SUPERVISOR_SAVED_S2(sp) /* Preserve caller s2. */
    sd s3, UMICOM_SUPERVISOR_SAVED_S3(sp) /* Preserve caller s3. */
    sd s4, UMICOM_SUPERVISOR_SAVED_S4(sp) /* Preserve caller s4. */
    sd s5, UMICOM_SUPERVISOR_SAVED_S5(sp) /* Preserve caller s5. */
    sd s6, UMICOM_SUPERVISOR_SAVED_S6(sp) /* Preserve caller s6. */
    sd s7, UMICOM_SUPERVISOR_SAVED_S7(sp) /* Preserve caller s7. */
    sd s8, UMICOM_SUPERVISOR_SAVED_S8(sp) /* Preserve caller s8. */
    sd s9, UMICOM_SUPERVISOR_SAVED_S9(sp) /* Preserve caller s9. */
    sd s10, UMICOM_SUPERVISOR_SAVED_S10(sp) /* Preserve caller s10. */
    sd s11, UMICOM_SUPERVISOR_SAVED_S11(sp) /* Preserve caller s11. */
    sd gp, UMICOM_SUPERVISOR_SAVED_GP(sp) /* Preserve caller gp. */
    sd tp, UMICOM_SUPERVISOR_SAVED_TP(sp) /* Preserve caller tp. */

    /* Keep both machine pointers off the supervisor stack. */
    sd a0, UMICOM_SUPERVISOR_SAVED_REQUEST(sp) /* Validated request. */
    sd a1, UMICOM_SUPERVISOR_SAVED_REPORT(sp)  /* Machine-owned report. */

    /* Save mstatus before clearing global MIE. Subsequent CSR changes happen
     * with interrupts masked, so there is no partially configured entry path. */
    csrr t0, mstatus
    sd t0, UMICOM_SUPERVISOR_SAVED_MSTATUS(sp)
    csrci mstatus, 8 /* Mask global machine interrupt delivery. */
    csrr t0, mie /* Read the caller's mie. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MIE(sp) /* Keep it for exact restoration. */
    csrr t0, mtvec /* Read the caller's mtvec. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MTVEC(sp) /* Keep it for exact restoration. */
    csrr t0, mscratch /* Read the caller's mscratch. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MSCRATCH(sp) /* Keep it for exact restoration. */
    csrr t0, medeleg /* Read the caller's medeleg. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MEDELEG(sp) /* Keep it for exact restoration. */
    csrr t0, mideleg /* Read the caller's mideleg. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MIDELEG(sp) /* Keep it for exact restoration. */
    csrr t0, satp /* Read the caller's satp. */
    sd t0, UMICOM_SUPERVISOR_SAVED_SATP(sp) /* Keep it for exact restoration. */
    csrr t0, pmpcfg0 /* Read the caller's pmpcfg0. */
    sd t0, UMICOM_SUPERVISOR_SAVED_PMPCFG0(sp) /* Keep it for exact restoration. */
    csrr t0, pmpaddr0 /* Read the caller's pmpaddr0. */
    sd t0, UMICOM_SUPERVISOR_SAVED_PMPADDR0(sp) /* Keep it for exact restoration. */
    csrr t0, mepc /* Read the caller's mepc. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MEPC(sp) /* Keep it for exact restoration. */
    csrr t0, mcause /* Read the caller's mcause. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MCAUSE(sp) /* Keep it for exact restoration. */
    csrr t0, mtval /* Read the caller's mtval. */
    sd t0, UMICOM_SUPERVISOR_SAVED_MTVAL(sp) /* Keep it for exact restoration. */

    /* Assume refusal until the supervisor actually traps back. A refusal leaves
     * no success report for the C harness to mistake for execution evidence. */
    li t0, 1
    sd t0, UMICOM_SUPERVISOR_SAVED_RESULT(sp)

    /* Do not overwrite a pre-existing permission policy. The current early
     * Kernel has no active PMP entries after its preceding MMU validation. */
    ld t0, UMICOM_SUPERVISOR_SAVED_PMPCFG0(sp)
    bnez t0, .Lumicom_supervisor_restore

    /* Refuse a caller already using effective-privilege data accesses. This
     * entry routine is deliberately narrower than a future general switch. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MSTATUS(sp)
    li t1, 0x20000 /* mstatus.MPRV. */
    and t0, t0, t1
    bnez t0, .Lumicom_supervisor_restore

    /* Keep the machine context pointer ready before installing the private
     * vector. Exception delegation is disabled only for this bounded call. */
    csrw mie, zero           /* No asynchronous event belongs to this test. */
    csrw mscratch, sp        /* Safe return stack, not a supervisor pointer. */
    la t0, UmicomRiscvSupervisorTrapEntry
    csrw mtvec, t0           /* Aligned address selects direct trap mode. */
    csrw medeleg, zero       /* Supervisor exceptions must reach machine mode. */
    csrw mideleg, zero       /* Do not introduce supervisor interrupt handlers. */

    /* The caller derives this NAPOT address from the actual managed RAM range.
     * No UART, timer or other MMIO address is granted to the payload. */
    ld t3, UMICOM_SUPERVISOR_SAVED_REQUEST(sp)
    ld t0, UMICOM_SUPERVISOR_REQUEST_PMP(t3)
    csrw pmpaddr0, t0        /* RAM region, without an immutable lock. */
    li t0, 0x1f             /* NAPOT address mode plus R/W/X, L remains zero. */
    csrw pmpcfg0, t0

    /* Construct Sv39 satp from the physical root frame. The C mapper has
     * already checked alignment and validated the complete hierarchy. */
    ld t0, UMICOM_SUPERVISOR_REQUEST_ROOT(t3)
    srli t0, t0, 12          /* Byte address becomes a physical page number. */
    li t1, 8
    slli t1, t1, 60         /* MODE=Sv39; ASID remains zero. */
    or t0, t0, t1
    csrw satp, t0
    sfence.vma zero, zero   /* Order page-table/PMP changes before S-mode use. */
    csrr t1, satp
    bne t0, t1, .Lumicom_supervisor_restore /* Refuse an unsupported WARL value. */

    /* These are the final request loads while sp still names the machine
     * context. After the swap below, no machine data is read in supervisor mode. */
    ld t0, UMICOM_SUPERVISOR_REQUEST_ENTRY(t3)
    csrw mepc, t0
    ld t4, UMICOM_SUPERVISOR_REQUEST_STACK(t3)
    ld a0, UMICOM_SUPERVISOR_REQUEST_OBSERVATION(t3)

    /* Select MPP=S. Clear MPRV, SUM, MXR, interrupt enables and the controls
     * that could forbid legitimate supervisor CSR access. This experiment
     * tests ordinary supervisor permissions, not inherited override bits. */
    csrr t0, mstatus
    li t1, 0x7e18aa         /* MPRV/SUM/MXR/TVM/TW/TSR, MPP, MIE/MPIE/SIE/SPIE. */
    not t1, t1
    and t0, t0, t1
    li t1, 0x800            /* MPP=01: supervisor. */
    or t0, t0, t1
    csrw mstatus, t0
    mv sp, t4               /* First use of the separately mapped stack. */
    mret                    /* Instruction fetch now uses Sv39 in supervisor mode. */

    /* This is a new, private vector. The established Kernel trap entry is
     * neither replaced nor modified; mtvec is restored before returning. */
    .balign 16
    .global UmicomRiscvSupervisorTrapEntry
    .type UmicomRiscvSupervisorTrapEntry, @function
UmicomRiscvSupervisorTrapEntry:
    /* No store may use the supervisor stack before this swap. mscratch now
     * holds the interrupted supervisor sp for the diagnostic report. */
    csrrw sp, mscratch, sp

    /* Clear live MPRV before any machine memory access. Capture the original
     * trap-entry status in t1 so the report can still prove MPP=S. */
    li t0, 0x20000
    csrrc t1, mstatus, t0
    /* A second fault while collecting the supervisor report must use neither
     * its old sp nor the first report frame. t0 is scratch at this point. */
    la t0, UmicomRiscvMachineTrapEmergency
    csrw mtvec, t0
    ld t2, UMICOM_SUPERVISOR_SAVED_REPORT(sp)
    sd t1, UMICOM_SUPERVISOR_REPORT_STATUS(t2)
    sd a0, UMICOM_SUPERVISOR_REPORT_RETURN(t2) /* Payload return register. */
    sd a7, UMICOM_SUPERVISOR_REPORT_COOKIE(t2) /* Payload completion cookie. */
    csrr t0, mcause
    sd t0, UMICOM_SUPERVISOR_REPORT_CAUSE(t2)
    csrr t0, mepc
    sd t0, UMICOM_SUPERVISOR_REPORT_PC(t2)
    csrr t0, mtval
    sd t0, UMICOM_SUPERVISOR_REPORT_VALUE(t2)
    csrr t0, mscratch
    sd t0, UMICOM_SUPERVISOR_REPORT_STACK(t2)

    /* Zero means a trap was captured, not that the operation was valid. The
     * C harness checks cause, PC, privilege, observations and final accounting. */
    sd zero, UMICOM_SUPERVISOR_SAVED_RESULT(sp)

.Lumicom_supervisor_restore:
    /* We are back in machine mode. Its instruction fetch is untranslated.
     * Restore the old translation root before any page-table frame is released. */
    ld t0, UMICOM_SUPERVISOR_SAVED_SATP(sp)
    csrw satp, t0
    sfence.vma zero, zero

    /* Remove our temporary permission rule before restoring its old address.
     * The entry was never locked, so the caller's exact configuration survives. */
    csrw pmpcfg0, zero
    ld t0, UMICOM_SUPERVISOR_SAVED_PMPADDR0(sp)
    csrw pmpaddr0, t0
    ld t0, UMICOM_SUPERVISOR_SAVED_PMPCFG0(sp)
    csrw pmpcfg0, t0
    sfence.vma zero, zero /* Invalidate permission-dependent translation state. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MEDELEG(sp)
    csrw medeleg, t0 /* Restore the caller's medeleg. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MIDELEG(sp)
    csrw mideleg, t0 /* Restore the caller's mideleg. */
    /* The saved vector and its scratch pointer form one entry contract.
     * Publish the pointer first, before making that vector active again. The
     * original scratch restoration below is retained and remains idempotent. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MSCRATCH(sp)
    csrw mscratch, t0
    ld t0, UMICOM_SUPERVISOR_SAVED_MTVEC(sp)
    csrw mtvec, t0 /* Restore the caller's mtvec. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MSCRATCH(sp)
    csrw mscratch, t0 /* Restore the caller's mscratch. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MEPC(sp)
    csrw mepc, t0 /* Restore the caller's mepc. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MCAUSE(sp)
    csrw mcause, t0 /* Restore the caller's mcause. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MTVAL(sp)
    csrw mtval, t0 /* Restore the caller's mtval. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MIE(sp)
    csrw mie, t0 /* Restore the caller's mie. */

    /* Read the result before releasing the machine frame. Only callee-saved
     * integer registers need restoring: the C ABI allows temporary/argument
     * registers to change during an ordinary function call. */
    ld a0, UMICOM_SUPERVISOR_SAVED_RESULT(sp)
    ld ra, UMICOM_SUPERVISOR_SAVED_RA(sp) /* Restore caller ra. */
    ld s0, UMICOM_SUPERVISOR_SAVED_S0(sp) /* Restore caller s0. */
    ld s1, UMICOM_SUPERVISOR_SAVED_S1(sp) /* Restore caller s1. */
    ld s2, UMICOM_SUPERVISOR_SAVED_S2(sp) /* Restore caller s2. */
    ld s3, UMICOM_SUPERVISOR_SAVED_S3(sp) /* Restore caller s3. */
    ld s4, UMICOM_SUPERVISOR_SAVED_S4(sp) /* Restore caller s4. */
    ld s5, UMICOM_SUPERVISOR_SAVED_S5(sp) /* Restore caller s5. */
    ld s6, UMICOM_SUPERVISOR_SAVED_S6(sp) /* Restore caller s6. */
    ld s7, UMICOM_SUPERVISOR_SAVED_S7(sp) /* Restore caller s7. */
    ld s8, UMICOM_SUPERVISOR_SAVED_S8(sp) /* Restore caller s8. */
    ld s9, UMICOM_SUPERVISOR_SAVED_S9(sp) /* Restore caller s9. */
    ld s10, UMICOM_SUPERVISOR_SAVED_S10(sp) /* Restore caller s10. */
    ld s11, UMICOM_SUPERVISOR_SAVED_S11(sp) /* Restore caller s11. */
    ld gp, UMICOM_SUPERVISOR_SAVED_GP(sp) /* Restore caller gp. */
    ld tp, UMICOM_SUPERVISOR_SAVED_TP(sp) /* Restore caller tp. */

    /* Restore global interrupt state last. MPRV was required to be clear on
     * entry, so releasing this stack frame cannot unexpectedly be translated. */
    ld t0, UMICOM_SUPERVISOR_SAVED_MSTATUS(sp)
    csrw mstatus, t0
    addi sp, sp, UMICOM_SUPERVISOR_CONTEXT_BYTES
    ret /* A machine-mode RET, not an MRET back into a faulting payload. */
    .size UmicomRiscvSupervisorTrapEntry, . - UmicomRiscvSupervisorTrapEntry
    .size UmicomRiscvSupervisorExecute, . - UmicomRiscvSupervisorExecute

    .global UmicomRiscvSupervisorMachineStateRead
    .type UmicomRiscvSupervisorMachineStateRead, @function
UmicomRiscvSupervisorMachineStateRead:
    /* Each read is non-destructive; the caller uses two snapshots to check
     * that lowering privilege did not leak control-state changes. */
    csrr t0, mstatus /* Observe mstatus. */
    sd t0, 0(a0) /* Publish its snapshot slot. */
    csrr t0, mie /* Observe mie. */
    sd t0, 8(a0) /* Publish its snapshot slot. */
    csrr t0, mtvec /* Observe mtvec. */
    sd t0, 16(a0) /* Publish its snapshot slot. */
    csrr t0, mscratch /* Observe mscratch. */
    sd t0, 24(a0) /* Publish its snapshot slot. */
    csrr t0, medeleg /* Observe medeleg. */
    sd t0, 32(a0) /* Publish its snapshot slot. */
    csrr t0, mideleg /* Observe mideleg. */
    sd t0, 40(a0) /* Publish its snapshot slot. */
    csrr t0, satp /* Observe satp. */
    sd t0, 48(a0) /* Publish its snapshot slot. */
    csrr t0, pmpcfg0 /* Observe pmpcfg0. */
    sd t0, 56(a0) /* Publish its snapshot slot. */
    csrr t0, pmpaddr0 /* Observe pmpaddr0. */
    sd t0, 64(a0) /* Publish its snapshot slot. */
    csrr t0, mepc /* Observe mepc. */
    sd t0, 72(a0) /* Publish its snapshot slot. */
    csrr t0, mcause /* Observe mcause. */
    sd t0, 80(a0) /* Publish its snapshot slot. */
    csrr t0, mtval /* Observe mtval. */
    sd t0, 88(a0) /* Publish its snapshot slot. */

    ret
    .size UmicomRiscvSupervisorMachineStateRead, . - UmicomRiscvSupervisorMachineStateRead

    /* Only this input section and the C payload section are mapped executable
     * in the supervisor page table. Other Kernel code stays machine-only. */
    .section .text.umicom_supervisor_payload, "ax", @progbits
    .balign 16
    .global UmicomRiscvSupervisorEntry
    .type UmicomRiscvSupervisorEntry, @function
UmicomRiscvSupervisorEntry:
    /* a0 is the mapped observation page; sp is the supervisor stack top.
     * An ordinary call now gives C a normal return address and calling frame. */
    call UmicomRiscvSupervisorPayload
    li a7, UMICOM_SUPERVISOR_COMPLETION_COOKIE
    .global UmicomRiscvSupervisorCompletionInstruction
UmicomRiscvSupervisorCompletionInstruction:
    ecall /* Cause 9 proves this instruction ran in supervisor mode. */
    j UmicomRiscvSupervisorCompletionInstruction /* Never fall into another function. */
    .size UmicomRiscvSupervisorEntry, . - UmicomRiscvSupervisorEntry

    .global UmicomRiscvSupervisorSatpRead
    .type UmicomRiscvSupervisorSatpRead, @function
UmicomRiscvSupervisorSatpRead:
    csrr a0, satp /* Supervisor code observes its real translation root. */
    ret
    .size UmicomRiscvSupervisorSatpRead, . - UmicomRiscvSupervisorSatpRead

    .global UmicomRiscvSupervisorStackRead
    .type UmicomRiscvSupervisorStackRead, @function
UmicomRiscvSupervisorStackRead:
    mv a0, sp /* Return a pointer within the live supervisor C stack frame. */
    ret
    .size UmicomRiscvSupervisorStackRead, . - UmicomRiscvSupervisorStackRead

    .global UmicomRiscvSupervisorGuardLoad
    .type UmicomRiscvSupervisorGuardLoad, @function
UmicomRiscvSupervisorGuardLoad:
    .global UmicomRiscvSupervisorGuardLoadInstruction
UmicomRiscvSupervisorGuardLoadInstruction:
    ld a0, 0(a0) /* Deliberately read the unmapped guard page. */
    ret /* Reaching this return would mean the expected protection failed. */
    .size UmicomRiscvSupervisorGuardLoad, . - UmicomRiscvSupervisorGuardLoad

    .global UmicomRiscvSupervisorReadOnlyStore
    .type UmicomRiscvSupervisorReadOnlyStore, @function
UmicomRiscvSupervisorReadOnlyStore:
    .global UmicomRiscvSupervisorReadOnlyStoreInstruction
UmicomRiscvSupervisorReadOnlyStoreInstruction:
    sd a1, 0(a0) /* Deliberately try to write through a read-only leaf. */
    ret /* An allowed store must not be reported as a protection success. */
    .size UmicomRiscvSupervisorReadOnlyStore, . - UmicomRiscvSupervisorReadOnlyStore

    .global UmicomRiscvSupervisorMachineStatusRead
    .type UmicomRiscvSupervisorMachineStatusRead, @function
UmicomRiscvSupervisorMachineStatusRead:
    .global UmicomRiscvSupervisorMachineStatusInstruction
UmicomRiscvSupervisorMachineStatusInstruction:
    csrr a0, mstatus /* Supervisor mode must not read a machine-only CSR. */
    ret
    .size UmicomRiscvSupervisorMachineStatusRead, . - UmicomRiscvSupervisorMachineStatusRead
