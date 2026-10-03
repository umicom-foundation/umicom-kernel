/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/trap.S
 *
 * PURPOSE:
 *   Provide the minimum machine-mode trap entry/return mechanism required by
 *   the machine trap and timer foundation, plus tiny architecture primitives for the deliberate exception and
 *   machine-timer test.
 *
 * EDUCATIONAL OVERVIEW:
 *   When a trap occurs, RISC-V hardware:
 *
 *     1. records the interrupted PC in mepc;
 *     2. records the cause in mcause;
 *     3. records additional information in mtval;
 *     4. moves the previous MIE state into MPIE and clears MIE;
 *     5. jumps to the address stored in mtvec.
 *
 *   Hardware does NOT push general-purpose registers to a stack.  This file
 *   performs that software save so an ordinary C23 function can inspect and
 *   handle the event without destroying the interrupted program state.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

    /* Keep trap code in its own executable section so tools can inspect it
     * independently from ordinary C-generated .text. */
    .section .text.trap, "ax", @progbits

    /* Align exported entry points on a 16-byte boundary.
     *
     * Direct-mode mtvec requires at least four-byte alignment.  Sixteen bytes
     * is intentionally stricter and keeps the teaching layout easy to inspect. */
    .align 4

    /* Byte size of UmicomRiscvTrapFrame in trap.h.
     *
     * 288 is divisible by 16, preserving the RISC-V ABI's required stack
     * alignment before we call a C function. */
    .equ UMICOM_TRAP_FRAME_SIZE, 288

    /* General-purpose register offsets inside UmicomRiscvTrapFrame.
     *
     * x0 has no slot because its architectural value is permanently zero. */
    .equ UMICOM_TRAP_X1_RA,    0
    .equ UMICOM_TRAP_X2_SP,    8
    .equ UMICOM_TRAP_X3_GP,   16
    .equ UMICOM_TRAP_X4_TP,   24
    .equ UMICOM_TRAP_X5_T0,   32
    .equ UMICOM_TRAP_X6_T1,   40
    .equ UMICOM_TRAP_X7_T2,   48
    .equ UMICOM_TRAP_X8_S0,   56
    .equ UMICOM_TRAP_X9_S1,   64
    .equ UMICOM_TRAP_X10_A0,  72
    .equ UMICOM_TRAP_X11_A1,  80
    .equ UMICOM_TRAP_X12_A2,  88
    .equ UMICOM_TRAP_X13_A3,  96
    .equ UMICOM_TRAP_X14_A4, 104
    .equ UMICOM_TRAP_X15_A5, 112
    .equ UMICOM_TRAP_X16_A6, 120
    .equ UMICOM_TRAP_X17_A7, 128
    .equ UMICOM_TRAP_X18_S2, 136
    .equ UMICOM_TRAP_X19_S3, 144
    .equ UMICOM_TRAP_X20_S4, 152
    .equ UMICOM_TRAP_X21_S5, 160
    .equ UMICOM_TRAP_X22_S6, 168
    .equ UMICOM_TRAP_X23_S7, 176
    .equ UMICOM_TRAP_X24_S8, 184
    .equ UMICOM_TRAP_X25_S9, 192
    .equ UMICOM_TRAP_X26_S10,200
    .equ UMICOM_TRAP_X27_S11,208
    .equ UMICOM_TRAP_X28_T3, 216
    .equ UMICOM_TRAP_X29_T4, 224
    .equ UMICOM_TRAP_X30_T5, 232
    .equ UMICOM_TRAP_X31_T6, 240

    /* CSR snapshot offsets inside the same frame. */
    .equ UMICOM_TRAP_MEPC,    248
    .equ UMICOM_TRAP_MSTATUS, 256
    .equ UMICOM_TRAP_MCAUSE,  264
    .equ UMICOM_TRAP_MTVAL,   272

    /* Export the installation helper so C23 startup can select this vector. */
    .global UmicomRiscvTrapInstall

    /* Mark the symbol as a function in ELF metadata. */
    .type UmicomRiscvTrapInstall, @function

UmicomRiscvTrapInstall:
    /* Clear the complete machine interrupt-enable register first.
     *
     * No interrupt source may be accepted while mtvec is being changed. */
    csrw mie, zero

    /* Load the global MIE mask (mstatus bit 3) into a temporary register. */
    li t0, 0x8

    /* Clear global machine interrupt delivery.
     *
     * csrc means "clear CSR bits named by the register operand". */
    csrc mstatus, t0

    /* Load the address of the Assembly trap entry into a temporary register. */
    la t0, UmicomRiscvTrapEntry

    /* Write mtvec in direct mode.
     *
     * UmicomRiscvTrapEntry is aligned so its low two bits are zero.  Therefore
     * mtvec.MODE=0 (Direct), and every M-mode trap enters at this one address. */
    csrw mtvec, t0

    /* Return to the C caller that requested trap installation. */
    ret

    /* Record function size for debuggers/symbol tools. */
    .size UmicomRiscvTrapInstall, . - UmicomRiscvTrapInstall


    /* Export a helper that reports the exact installed vector address. */
    .global UmicomRiscvTrapVectorAddress
    .type UmicomRiscvTrapVectorAddress, @function

UmicomRiscvTrapVectorAddress:
    /* Place the link-time address in a0, the first integer return register. */
    la a0, UmicomRiscvTrapEntry

    /* Return that address to the C caller. */
    ret

    .size UmicomRiscvTrapVectorAddress, . - UmicomRiscvTrapVectorAddress


    /* Export the actual mtvec direct-mode entry. */
    .global UmicomRiscvTrapEntry
    .type UmicomRiscvTrapEntry, @function

    /* Keep the vector entry strongly aligned for mtvec and human inspection. */
    .align 4

UmicomRiscvTrapEntry:
    /* Reserve the complete software trap frame below the interrupted stack.
     *
     * RISC-V stacks grow toward lower addresses. */
    addi sp, sp, -UMICOM_TRAP_FRAME_SIZE

    /* Save ra/x1 before any C call can overwrite the return-address register. */
    sd ra, UMICOM_TRAP_X1_RA(sp)

    /* Save gp/x3 even though disables the small-data ABI.
     *
     * A trap mechanism should preserve architectural state rather than relying
     * on the current compiler happenstance. */
    sd gp, UMICOM_TRAP_X3_GP(sp)

    /* Save tp/x4 for the same reason; future per-hart code may use it. */
    sd tp, UMICOM_TRAP_X4_TP(sp)

    /* Save t0/x5 before using t0 as a scratch register below. */
    sd t0, UMICOM_TRAP_X5_T0(sp)

    /* Reconstruct the interrupted stack pointer.
     *
     * sp now points to the new frame.  Adding the frame size yields the exact
     * value sp held before the trap occurred. */
    addi t0, sp, UMICOM_TRAP_FRAME_SIZE

    /* Store that original sp/x2 value into the frame for diagnostics/debugging. */
    sd t0, UMICOM_TRAP_X2_SP(sp)

    /* Save the remaining caller/saved/argument/temporary integer registers.
     *
     * Saving all of them allows the C dispatcher to behave like an ordinary C
     * function without corrupting the interrupted context. */
    sd t1, UMICOM_TRAP_X6_T1(sp)       /* Preserve x6/t1. */
    sd t2, UMICOM_TRAP_X7_T2(sp)       /* Preserve x7/t2. */
    sd s0, UMICOM_TRAP_X8_S0(sp)       /* Preserve x8/s0 (frame pointer). */
    sd s1, UMICOM_TRAP_X9_S1(sp)       /* Preserve x9/s1. */
    sd a0, UMICOM_TRAP_X10_A0(sp)      /* Preserve x10/a0 argument/return. */
    sd a1, UMICOM_TRAP_X11_A1(sp)      /* Preserve x11/a1. */
    sd a2, UMICOM_TRAP_X12_A2(sp)      /* Preserve x12/a2. */
    sd a3, UMICOM_TRAP_X13_A3(sp)      /* Preserve x13/a3. */
    sd a4, UMICOM_TRAP_X14_A4(sp)      /* Preserve x14/a4. */
    sd a5, UMICOM_TRAP_X15_A5(sp)      /* Preserve x15/a5. */
    sd a6, UMICOM_TRAP_X16_A6(sp)      /* Preserve x16/a6. */
    sd a7, UMICOM_TRAP_X17_A7(sp)      /* Preserve x17/a7. */
    sd s2, UMICOM_TRAP_X18_S2(sp)      /* Preserve x18/s2. */
    sd s3, UMICOM_TRAP_X19_S3(sp)      /* Preserve x19/s3. */
    sd s4, UMICOM_TRAP_X20_S4(sp)      /* Preserve x20/s4. */
    sd s5, UMICOM_TRAP_X21_S5(sp)      /* Preserve x21/s5. */
    sd s6, UMICOM_TRAP_X22_S6(sp)      /* Preserve x22/s6. */
    sd s7, UMICOM_TRAP_X23_S7(sp)      /* Preserve x23/s7. */
    sd s8, UMICOM_TRAP_X24_S8(sp)      /* Preserve x24/s8. */
    sd s9, UMICOM_TRAP_X25_S9(sp)      /* Preserve x25/s9. */
    sd s10, UMICOM_TRAP_X26_S10(sp)    /* Preserve x26/s10. */
    sd s11, UMICOM_TRAP_X27_S11(sp)    /* Preserve x27/s11. */
    sd t3, UMICOM_TRAP_X28_T3(sp)      /* Preserve x28/t3. */
    sd t4, UMICOM_TRAP_X29_T4(sp)      /* Preserve x29/t4. */
    sd t5, UMICOM_TRAP_X30_T5(sp)      /* Preserve x30/t5. */
    sd t6, UMICOM_TRAP_X31_T6(sp)      /* Preserve x31/t6. */

    /* Read mepc, which identifies where execution was interrupted/trapped. */
    csrr t0, mepc

    /* Preserve mepc in the software frame so C can inspect or deliberately
     * change the return PC for a handled synchronous exception. */
    sd t0, UMICOM_TRAP_MEPC(sp)

    /* Read machine status after hardware trap entry. */
    csrr t0, mstatus

    /* Save mstatus so MRET can restore the intended privilege/interrupt state. */
    sd t0, UMICOM_TRAP_MSTATUS(sp)

    /* Clear MPRV only in the live machine-status CSR before calling C.
     *
     * Most traps arrive while MPRV is already clear.  Hardware-translation
     * validation is different: one deliberate load/store temporarily sets
     * MPRV so it is checked as supervisor-mode memory access.  If that access
     * faults, leaving live MPRV set while the trap handler prints diagnostics
     * could make the handler's own RAM/MMIO accesses use the failing page
     * tables and recursively trap.
     *
     * The original mstatus value remains intact in the software frame above.
     * A returning trap can therefore restore the interrupted state exactly.
     * Fatal translation faults do not return; they can still report safely
     * because the live handler executes with ordinary machine-mode accesses. */
    li t0, 0x20000
    csrc mstatus, t0

    /* Read mcause to distinguish exception versus interrupt and identify code. */
    csrr t0, mcause

    /* Save mcause for the C23 policy dispatcher. */
    sd t0, UMICOM_TRAP_MCAUSE(sp)

    /* Read mtval, whose meaning depends on the particular trap cause. */
    csrr t0, mtval

    /* Save mtval for evidence and future fault diagnostics. */
    sd t0, UMICOM_TRAP_MTVAL(sp)

    /* Pass a pointer to the complete frame as the first C argument (a0). */
    mv a0, sp

    /* Call C23 trap policy while hardware MIE remains cleared by trap entry. */
    call UmicomRiscvTrapDispatch

    /* Reload the possibly modified return PC from the frame.
     *
     * the deliberate ECALL handler advances this field by four bytes so MRET
     * resumes after ECALL instead of immediately trapping on it again. */
    ld t0, UMICOM_TRAP_MEPC(sp)

    /* Publish the selected return PC back to the architectural mepc CSR. */
    csrw mepc, t0

    /* Reload the captured/possibly adjusted machine status. */
    ld t0, UMICOM_TRAP_MSTATUS(sp)

    /* Restore mstatus before MRET interprets MPIE/MPP. */
    csrw mstatus, t0

    /* Restore all saved integer registers except sp.
     *
     * t0 is restored near the end because it was our CSR scratch register. */
    ld ra, UMICOM_TRAP_X1_RA(sp)       /* Restore x1/ra. */
    ld gp, UMICOM_TRAP_X3_GP(sp)       /* Restore x3/gp. */
    ld tp, UMICOM_TRAP_X4_TP(sp)       /* Restore x4/tp. */
    ld t1, UMICOM_TRAP_X6_T1(sp)       /* Restore x6/t1. */
    ld t2, UMICOM_TRAP_X7_T2(sp)       /* Restore x7/t2. */
    ld s0, UMICOM_TRAP_X8_S0(sp)       /* Restore x8/s0. */
    ld s1, UMICOM_TRAP_X9_S1(sp)       /* Restore x9/s1. */
    ld a0, UMICOM_TRAP_X10_A0(sp)      /* Restore x10/a0. */
    ld a1, UMICOM_TRAP_X11_A1(sp)      /* Restore x11/a1. */
    ld a2, UMICOM_TRAP_X12_A2(sp)      /* Restore x12/a2. */
    ld a3, UMICOM_TRAP_X13_A3(sp)      /* Restore x13/a3. */
    ld a4, UMICOM_TRAP_X14_A4(sp)      /* Restore x14/a4. */
    ld a5, UMICOM_TRAP_X15_A5(sp)      /* Restore x15/a5. */
    ld a6, UMICOM_TRAP_X16_A6(sp)      /* Restore x16/a6. */
    ld a7, UMICOM_TRAP_X17_A7(sp)      /* Restore x17/a7. */
    ld s2, UMICOM_TRAP_X18_S2(sp)      /* Restore x18/s2. */
    ld s3, UMICOM_TRAP_X19_S3(sp)      /* Restore x19/s3. */
    ld s4, UMICOM_TRAP_X20_S4(sp)      /* Restore x20/s4. */
    ld s5, UMICOM_TRAP_X21_S5(sp)      /* Restore x21/s5. */
    ld s6, UMICOM_TRAP_X22_S6(sp)      /* Restore x22/s6. */
    ld s7, UMICOM_TRAP_X23_S7(sp)      /* Restore x23/s7. */
    ld s8, UMICOM_TRAP_X24_S8(sp)      /* Restore x24/s8. */
    ld s9, UMICOM_TRAP_X25_S9(sp)      /* Restore x25/s9. */
    ld s10, UMICOM_TRAP_X26_S10(sp)    /* Restore x26/s10. */
    ld s11, UMICOM_TRAP_X27_S11(sp)    /* Restore x27/s11. */
    ld t3, UMICOM_TRAP_X28_T3(sp)      /* Restore x28/t3. */
    ld t4, UMICOM_TRAP_X29_T4(sp)      /* Restore x29/t4. */
    ld t5, UMICOM_TRAP_X30_T5(sp)      /* Restore x30/t5. */
    ld t6, UMICOM_TRAP_X31_T6(sp)      /* Restore x31/t6. */

    /* Restore the original interrupted t0/x5 value last. */
    ld t0, UMICOM_TRAP_X5_T0(sp)

    /* Release the software trap frame.
     *
     * This arithmetic restores sp to exactly the interrupted value saved in
     * the diagnostic x2 field. */
    addi sp, sp, UMICOM_TRAP_FRAME_SIZE

    /* Return from the machine-mode trap.
     *
     * MRET resumes at mepc and restores MIE from MPIE according to the RISC-V
     * privileged architecture. */
    mret

    .size UmicomRiscvTrapEntry, . - UmicomRiscvTrapEntry


    /* Export a deliberate synchronous-exception trigger used by trap validation. */
    .global UmicomRiscvTriggerMachineEcall
    .type UmicomRiscvTriggerMachineEcall, @function

UmicomRiscvTriggerMachineEcall:
    /* ECALL executed in M-mode raises synchronous exception code 11.
     *
     * ECALL is a 32-bit SYSTEM instruction, so the handler can safely move
     * mepc forward by exactly four bytes after it recognises this deliberate
     * test exception. */
    ecall

    /* If the handler advanced mepc correctly, execution resumes here. */
    ret

    .size UmicomRiscvTriggerMachineEcall, . - UmicomRiscvTriggerMachineEcall


    /* Export the timer/global interrupt enable helper. */
    .global UmicomRiscvMachineTimerInterruptEnable
    .type UmicomRiscvMachineTimerInterruptEnable, @function

UmicomRiscvMachineTimerInterruptEnable:
    /* Load mie.MTIE (bit 7) because CSR immediate operations can encode only a
     * five-bit immediate and therefore cannot directly name bit 7. */
    li t0, 0x80

    /* Enable the machine-timer interrupt source in mie. */
    csrs mie, t0

    /* Load mstatus.MIE (bit 3), the global M-mode interrupt-enable switch. */
    li t0, 0x8

    /* Enable global machine-mode interrupt delivery. */
    csrs mstatus, t0

    /* Return to the caller, which can now execute WFI awaiting the timer. */
    ret

    .size UmicomRiscvMachineTimerInterruptEnable, . - UmicomRiscvMachineTimerInterruptEnable


    /* Export the corresponding disable helper. */
    .global UmicomRiscvMachineTimerInterruptDisable
    .type UmicomRiscvMachineTimerInterruptDisable, @function

UmicomRiscvMachineTimerInterruptDisable:
    /* Load the mie.MTIE mask. */
    li t0, 0x80

    /* Stop machine-timer delivery at the per-source enable register. */
    csrc mie, t0

    /* Load the global mstatus.MIE mask. */
    li t0, 0x8

    /* Disable global machine-mode interrupt delivery as returns to a
     * deterministic polling/finish state. */
    csrc mstatus, t0

    /* Return after both enable layers are disabled. */
    ret

    .size UmicomRiscvMachineTimerInterruptDisable, . - UmicomRiscvMachineTimerInterruptDisable


    /* Export one explicit Wait For Interrupt primitive. */
    .global UmicomRiscvWaitForInterrupt
    .type UmicomRiscvWaitForInterrupt, @function

UmicomRiscvWaitForInterrupt:
    /* WFI allows the hart to wait efficiently for an enabled interrupt/event.
     *
     * uses it only after programming mtimecmp and enabling MTIE+MIE. */
    wfi

    /* Once the interrupt handler returns through MRET, return to the C caller. */
    ret

    .size UmicomRiscvWaitForInterrupt, . - UmicomRiscvWaitForInterrupt


    /* Export a tiny mhartid reader needed by the timer handler. */
    .global UmicomRiscvReadHartId
    .type UmicomRiscvReadHartId, @function

UmicomRiscvReadHartId:
    /* Read the current hart identifier directly into the integer return
     * register a0. */
    csrr a0, mhartid

    /* Return the hart identifier to C. */
    ret

    .size UmicomRiscvReadHartId, . - UmicomRiscvReadHartId


    /*-------------------------------------------------------------------------
     * HISTORICAL SHORT ELF SYMBOLS — RETAINED FOR REVIEW, NOT COMPILED
     *
     * The first trap implementation exported `UmiRiscv...` symbol names.
     * The active Assembly entry points above are now the full
     * `UmicomRiscv...` symbols.  Preserve the old alias statements below for
     * educational comparison, but keep them disabled so the produced ELF
     * contains only the canonical full-name interfaces.
     *-------------------------------------------------------------------------*/
#if 0
    /*-------------------------------------------------------------------------
     * LEGACY SYMBOL ALIASES
     *
     * These ELF symbol aliases preserve references from earlier experiments
     * without duplicating trap logic.  The actual implementation and all new
     * source use the full Umicom names above.
     *-------------------------------------------------------------------------*/
    .global UmiRiscvTrapInstall
    .set UmiRiscvTrapInstall, UmicomRiscvTrapInstall

    .global UmiRiscvTrapVectorAddress
    .set UmiRiscvTrapVectorAddress, UmicomRiscvTrapVectorAddress

    .global UmiRiscvTrapEntry
    .set UmiRiscvTrapEntry, UmicomRiscvTrapEntry

    .global UmiRiscvTriggerMachineEcall
    .set UmiRiscvTriggerMachineEcall, UmicomRiscvTriggerMachineEcall

    .global UmiRiscvMachineTimerInterruptEnable
    .set UmiRiscvMachineTimerInterruptEnable, UmicomRiscvMachineTimerInterruptEnable

    .global UmiRiscvMachineTimerInterruptDisable
    .set UmiRiscvMachineTimerInterruptDisable, UmicomRiscvMachineTimerInterruptDisable

    .global UmiRiscvWaitForInterrupt
    .set UmiRiscvWaitForInterrupt, UmicomRiscvWaitForInterrupt

    .global UmiRiscvReadHartId
    .set UmiRiscvReadHartId, UmicomRiscvReadHartId
#endif
