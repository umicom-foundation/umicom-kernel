/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/interrupt_state.S
 *
 * PURPOSE:
 *   Save/mask one hart's interrupt delivery without restoring stale privilege
 *   fields, and expose the CSR observations used by the ownership policy.
 *
 * EDUCATIONAL OVERVIEW:
 *   A read followed by a separate clear leaves a window for an interrupt.
 *   CSRRCI captures the old MIE and clears it in one architectural operation.
 *   The fences order local memory/device accesses around that boundary. They
 *   do not turn hart-local exclusion into an SMP lock, stop DMA, or mask faults.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
    .section .text.umicom_interrupt_state, "ax", @progbits
    .balign 4
    .global UmicomRiscvInterruptMaskSave
    .type UmicomRiscvInterruptMaskSave, @function
UmicomRiscvInterruptMaskSave:
    csrrci a0, mstatus, 8 /* Capture and clear MIE without an intervening window. */
    fence iorw, iorw      /* Order device/RAM work before accessing guarded state. */
    andi a0, a0, 8       /* Do not hand the caller a stale complete mstatus word. */
    ret
    .size UmicomRiscvInterruptMaskSave, . - UmicomRiscvInterruptMaskSave

    .global UmicomRiscvInterruptMaskRestore
    .type UmicomRiscvInterruptMaskRestore, @function
UmicomRiscvInterruptMaskRestore:
    fence iorw, iorw      /* Publish protected writes before an ISR can run. */
    andi a0, a0, 8       /* Only MIE belongs to this primitive. */
    beqz a0, .Lumicom_interrupt_keep_masked
    csrsi mstatus, 8      /* A pending interrupt may arrive immediately here. */
    ret                  /* Nothing after publication mutates protected state. */
.Lumicom_interrupt_keep_masked:
    csrci mstatus, 8      /* Preserve an originally disabled caller. */
    ret
    .size UmicomRiscvInterruptMaskRestore, . - UmicomRiscvInterruptMaskRestore

    .global UmicomRiscvInterruptStateRead
    .type UmicomRiscvInterruptStateRead, @function
UmicomRiscvInterruptStateRead:
    csrr t0, mhartid
    sd t0, 0(a0)         /* Hart identity belongs to hardware, not an argument. */
    csrr t0, mstatus
    sd t0, 8(a0)         /* Caller has already cleared global delivery. */
    csrr t0, mie
    sd t0, 16(a0)        /* Sources enabled independently of global MIE. */
    csrr t0, mip
    sd t0, 24(a0)        /* Pending sources remain visible while masked. */
    csrr t0, mtvec
    sd t0, 32(a0)        /* Vector and scratch must remain a matched pair. */
    csrr t0, mscratch
    sd t0, 40(a0)        /* Expected landing stack, not a borrowed user stack. */
    csrr t0, satp
    sd t0, 48(a0)        /* This service currently requires Bare translation. */
    csrr t0, mideleg
    sd t0, 56(a0)        /* Do not claim a source delegated elsewhere. */
    ret
    .size UmicomRiscvInterruptStateRead, . - UmicomRiscvInterruptStateRead

    .global UmicomRiscvInterruptSourcesEnable
    .type UmicomRiscvInterruptSourcesEnable, @function
UmicomRiscvInterruptSourcesEnable:
    fence iorw, iorw      /* Observe device setup before permitting its source. */
    csrs mie, a0         /* Leave all other source bits alone. */
    ret
    .size UmicomRiscvInterruptSourcesEnable, . - UmicomRiscvInterruptSourcesEnable

    .global UmicomRiscvInterruptSourcesDisable
    .type UmicomRiscvInterruptSourcesDisable, @function
UmicomRiscvInterruptSourcesDisable:
    csrc mie, a0         /* Masking is not an acknowledgement of mip/device state. */
    fence iorw, iorw      /* Order masking before the owner tears down its source. */
    ret
    .size UmicomRiscvInterruptSourcesDisable, . - UmicomRiscvInterruptSourcesDisable
