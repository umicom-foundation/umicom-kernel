/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/trap_entry.S
 *
 * PURPOSE:
 *   Land machine traps on owned storage, preserve the interrupted integer
 *   state, and stop a second trap without overwriting the first frame.
 *
 * EDUCATIONAL OVERVIEW:
 *   mscratch is the landing pointer whenever the ordinary machine vector is
 *   installed. A trap exchanges it with sp before any memory access. A bad
 *   interrupted sp is therefore a report value, not the address of our frame.
 *   Private user/supervisor vectors own mscratch only while they are installed.
 *
 *   This vector does not nest. Once the first scratch register is saved, mtvec
 *   is redirected to a separate emergency stack until return is complete.
 *   Those first stores require valid Kernel RAM and an intact landing pointer;
 *   no software protocol can recover from every possible hardware failure.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/trap_integrity.h"

    .section .text.umicom_machine_trap, "ax", @progbits
    .balign 16
    .global UmicomRiscvMachineTrapPrepare
    .type UmicomRiscvMachineTrapPrepare, @function
UmicomRiscvMachineTrapPrepare:
    /* TrapInstall has masked interrupts and still owns a valid caller stack.
     * Initialise both margins before publishing the landing pointer. */
    li t1, UMICOM_TRAP_INTEGRITY_CANARY
    la t0, UmicomRiscvMachineTrapStackLow
    sd t1, 0(t0)             /* Lower sentinel detects an exhausted handler stack. */
    not t2, t1
    sd t2, 8(t0)             /* Its complement catches a repeated-byte overwrite. */
    la t0, UmicomRiscvMachineTrapStackTop
    sd t1, 0(t0)             /* The upper sentinel is outside the usable stack. */
    sd t2, 8(t0)
    csrw mscratch, t0        /* Publish only after the reserved storage is ready. */
    ret
    .size UmicomRiscvMachineTrapPrepare, . - UmicomRiscvMachineTrapPrepare

    .balign 16
    .global UmicomRiscvMachineTrapEnter
    .type UmicomRiscvMachineTrapEnter, @function
UmicomRiscvMachineTrapEnter:
    /* Do not dereference the interrupted stack, even to save one register. */
    csrrw sp, mscratch, sp
    addi sp, sp, -UMICOM_TRAP_INTEGRITY_FRAME_BYTES
    sd t0, 32(sp)            /* Save x5 before borrowing it to change mtvec. */
    la t0, UmicomRiscvMachineTrapEmergency
    csrw mtvec, t0           /* A further exception must not overwrite this frame. */
    sd ra, 0(sp)        /* Preserve interrupted x1/ra. */
    sd gp, 16(sp)       /* Preserve interrupted x3/gp. */
    sd tp, 24(sp)       /* Preserve interrupted x4/tp. */
    sd t1, 40(sp)       /* Preserve interrupted x6/t1. */
    sd t2, 48(sp)       /* Preserve interrupted x7/t2. */
    sd s0, 56(sp)       /* Preserve interrupted x8/s0. */
    sd s1, 64(sp)       /* Preserve interrupted x9/s1. */
    sd a0, 72(sp)       /* Preserve interrupted x10/a0. */
    sd a1, 80(sp)       /* Preserve interrupted x11/a1. */
    sd a2, 88(sp)       /* Preserve interrupted x12/a2. */
    sd a3, 96(sp)       /* Preserve interrupted x13/a3. */
    sd a4, 104(sp)      /* Preserve interrupted x14/a4. */
    sd a5, 112(sp)      /* Preserve interrupted x15/a5. */
    sd a6, 120(sp)      /* Preserve interrupted x16/a6. */
    sd a7, 128(sp)      /* Preserve interrupted x17/a7. */
    sd s2, 136(sp)      /* Preserve interrupted x18/s2. */
    sd s3, 144(sp)      /* Preserve interrupted x19/s3. */
    sd s4, 152(sp)      /* Preserve interrupted x20/s4. */
    sd s5, 160(sp)      /* Preserve interrupted x21/s5. */
    sd s6, 168(sp)      /* Preserve interrupted x22/s6. */
    sd s7, 176(sp)      /* Preserve interrupted x23/s7. */
    sd s8, 184(sp)      /* Preserve interrupted x24/s8. */
    sd s9, 192(sp)      /* Preserve interrupted x25/s9. */
    sd s10, 200(sp)     /* Preserve interrupted x26/s10. */
    sd s11, 208(sp)     /* Preserve interrupted x27/s11. */
    sd t3, 216(sp)      /* Preserve interrupted x28/t3. */
    sd t4, 224(sp)      /* Preserve interrupted x29/t4. */
    sd t5, 232(sp)      /* Preserve interrupted x30/t5. */
    sd t6, 240(sp)      /* Preserve interrupted x31/t6. */
    csrr t0, mscratch       /* Old sp is captured as a value, not dereferenced. */
    sd t0, 8(sp)
    csrr t0, mepc
    sd t0, UMICOM_TRAP_INTEGRITY_PC(sp)
    csrr t0, mstatus
    sd t0, UMICOM_TRAP_INTEGRITY_STATUS(sp)
    li t0, 0x20000
    csrc mstatus, t0         /* Handler memory accesses must not inherit MPRV. */
    csrr t0, mcause
    sd t0, UMICOM_TRAP_INTEGRITY_CAUSE(sp)
    csrr t0, mtval
    sd t0, UMICOM_TRAP_INTEGRITY_VALUE(sp)
    sd zero, UMICOM_TRAP_INTEGRITY_RESERVED(sp)

    /* The wrapper validates the landing address and policy result. It calls
     * the original cause dispatcher for ordinary ECALL and timer events. */
    mv a0, sp
    call UmicomRiscvMachineTrapDispatchGuarded

    /* The checked result has MPP=M, MIE=0 and MPRV=0. Writing it here cannot
     * enable interrupts or translate the remaining frame loads. MRET is the
     * only instruction that may publish the saved MPIE state as live MIE. */
    ld t0, UMICOM_TRAP_INTEGRITY_PC(sp)
    csrw mepc, t0
    ld t0, UMICOM_TRAP_INTEGRITY_STATUS(sp)
    csrw mstatus, t0
    ld ra, 0(sp)        /* Restore interrupted x1/ra. */
    ld gp, 16(sp)       /* Restore interrupted x3/gp. */
    ld tp, 24(sp)       /* Restore interrupted x4/tp. */
    ld t1, 40(sp)       /* Restore interrupted x6/t1. */
    ld t2, 48(sp)       /* Restore interrupted x7/t2. */
    ld s0, 56(sp)       /* Restore interrupted x8/s0. */
    ld s1, 64(sp)       /* Restore interrupted x9/s1. */
    ld a0, 72(sp)       /* Restore interrupted x10/a0. */
    ld a1, 80(sp)       /* Restore interrupted x11/a1. */
    ld a2, 88(sp)       /* Restore interrupted x12/a2. */
    ld a3, 96(sp)       /* Restore interrupted x13/a3. */
    ld a4, 104(sp)      /* Restore interrupted x14/a4. */
    ld a5, 112(sp)      /* Restore interrupted x15/a5. */
    ld a6, 120(sp)      /* Restore interrupted x16/a6. */
    ld a7, 128(sp)      /* Restore interrupted x17/a7. */
    ld s2, 136(sp)      /* Restore interrupted x18/s2. */
    ld s3, 144(sp)      /* Restore interrupted x19/s3. */
    ld s4, 152(sp)      /* Restore interrupted x20/s4. */
    ld s5, 160(sp)      /* Restore interrupted x21/s5. */
    ld s6, 168(sp)      /* Restore interrupted x22/s6. */
    ld s7, 176(sp)      /* Restore interrupted x23/s7. */
    ld s8, 184(sp)      /* Restore interrupted x24/s8. */
    ld s9, 192(sp)      /* Restore interrupted x25/s9. */
    ld s10, 200(sp)     /* Restore interrupted x26/s10. */
    ld s11, 208(sp)     /* Restore interrupted x27/s11. */
    ld t3, 216(sp)      /* Restore interrupted x28/t3. */
    ld t4, 224(sp)      /* Restore interrupted x29/t4. */
    ld t5, 232(sp)      /* Restore interrupted x30/t5. */
    ld t6, 240(sp)      /* Restore interrupted x31/t6. */
    /* Restore the ordinary vector and its matching scratch owner while global
     * interrupts remain masked. The final frame loads use valid owned RAM. */
    la t0, UmicomRiscvMachineTrapStackTop
    csrw mscratch, t0
    la t0, UmicomRiscvTrapEntry
    csrw mtvec, t0
    ld t0, 32(sp)            /* Restore the temporary used for all CSR writes. */
    ld sp, 8(sp)             /* Restore even a deliberately invalid interrupted sp. */
    mret                    /* The resumed instruction decides how to use its sp. */
    .size UmicomRiscvMachineTrapEnter, . - UmicomRiscvMachineTrapEnter

    .balign 16
    .global UmicomRiscvMachineTrapEmergency
    .type UmicomRiscvMachineTrapEmergency, @function
UmicomRiscvMachineTrapEmergency:
    /* This path will not return. It does not borrow mscratch or trust the
     * current sp. Capture the second fault before changing the live status. */
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    csrr a3, mstatus
    csrw mie, zero           /* No asynchronous source may interrupt diagnostics. */
    li t0, 0x20008
    csrc mstatus, t0         /* Clear MPRV and global MIE before touching RAM. */
    la t0, UmicomPlatformTrapEmergencyHalt
    csrw mtvec, t0           /* A fault in diagnostics goes directly to fail-stop. */
    la sp, UmicomRiscvTrapEmergencyStackTop
    call UmicomKernelTrapEmergencyStop
    j UmicomPlatformTrapEmergencyHalt /* Defensive only: the C contract is noreturn. */
    .size UmicomRiscvMachineTrapEmergency, . - UmicomRiscvMachineTrapEmergency

    /* These stacks are reserved as part of the Kernel image, so the existing
     * physical allocator cannot hand them to a loaded program. BSS initialises
     * their storage; Prepare installs sentinels before the vector becomes live. */
    .section .bss.umicom_machine_trap_stack, "aw", @nobits
    .balign 16
    .global UmicomRiscvMachineTrapStackLow
UmicomRiscvMachineTrapStackLow:
    .space 16
    .space UMICOM_TRAP_INTEGRITY_STACK_BYTES
    .global UmicomRiscvMachineTrapStackTop
UmicomRiscvMachineTrapStackTop:
    .space 16
    .balign 16
    .space 4096             /* Emergency diagnostics have a separate bounded stack. */
UmicomRiscvTrapEmergencyStackTop:
