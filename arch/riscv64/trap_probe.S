/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/trap_probe.S
 *
 * PURPOSE:
 *   Exercise the actual machine vector with distinct integer-register values,
 *   an unusable interrupted stack, and deliberately faulting MPRV accesses.
 *
 * EDUCATIONAL NOTE:
 *   The probe borrows sscratch to remember its valid caller stack while sp is
 *   deliberately zero. It restores sscratch and the caller's callee-saved state
 *   before returning. This is a hart-zero, interrupts-controlled test, not a
 *   callable user service. Machine trap entry itself does not borrow sscratch.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
    .section .text.umicom_trap_probe, "ax", @progbits
    .balign 16

    .global UmicomRiscvTrapRegistersProbe
    .type UmicomRiscvTrapRegistersProbe, @function
UmicomRiscvTrapRegistersProbe:
    addi sp, sp, -128        /* Preserve an aligned machine caller frame. */
    sd ra, 0(sp) /* Preserve caller ra. */
    sd gp, 8(sp) /* Preserve caller gp. */
    sd tp, 16(sp) /* Preserve caller tp. */
    sd s0, 24(sp) /* Preserve caller s0. */
    sd s1, 32(sp) /* Preserve caller s1. */
    sd s2, 40(sp) /* Preserve caller s2. */
    sd s3, 48(sp) /* Preserve caller s3. */
    sd s4, 56(sp) /* Preserve caller s4. */
    sd s5, 64(sp) /* Preserve caller s5. */
    sd s6, 72(sp) /* Preserve caller s6. */
    sd s7, 80(sp) /* Preserve caller s7. */
    sd s8, 88(sp) /* Preserve caller s8. */
    sd s9, 96(sp) /* Preserve caller s9. */
    sd s10, 104(sp) /* Preserve caller s10. */
    sd s11, 112(sp) /* Preserve caller s11. */
    csrr t0, sscratch
    sd t0, 120(sp)           /* Do not leave a borrowed supervisor CSR changed. */
    csrw sscratch, sp        /* Trusted caller stack survives the zero-sp test. */
    li ra, 1 /* Distinct expected value for interrupted x1. */
    li gp, 3 /* Distinct expected value for interrupted x3. */
    li tp, 4 /* Distinct expected value for interrupted x4. */
    li t0, 5 /* Distinct expected value for interrupted x5. */
    li t1, 6 /* Distinct expected value for interrupted x6. */
    li t2, 7 /* Distinct expected value for interrupted x7. */
    li s0, 8 /* Distinct expected value for interrupted x8. */
    li s1, 9 /* Distinct expected value for interrupted x9. */
    li a0, 10 /* Distinct expected value for interrupted x10. */
    li a1, 11 /* Distinct expected value for interrupted x11. */
    li a2, 12 /* Distinct expected value for interrupted x12. */
    li a3, 13 /* Distinct expected value for interrupted x13. */
    li a4, 14 /* Distinct expected value for interrupted x14. */
    li a5, 15 /* Distinct expected value for interrupted x15. */
    li a6, 16 /* Distinct expected value for interrupted x16. */
    li a7, 17 /* Distinct expected value for interrupted x17. */
    li s2, 18 /* Distinct expected value for interrupted x18. */
    li s3, 19 /* Distinct expected value for interrupted x19. */
    li s4, 20 /* Distinct expected value for interrupted x20. */
    li s5, 21 /* Distinct expected value for interrupted x21. */
    li s6, 22 /* Distinct expected value for interrupted x22. */
    li s7, 23 /* Distinct expected value for interrupted x23. */
    li s8, 24 /* Distinct expected value for interrupted x24. */
    li s9, 25 /* Distinct expected value for interrupted x25. */
    li s10, 26 /* Distinct expected value for interrupted x26. */
    li s11, 27 /* Distinct expected value for interrupted x27. */
    li t3, 28 /* Distinct expected value for interrupted x28. */
    li t4, 29 /* Distinct expected value for interrupted x29. */
    li t5, 30 /* Distinct expected value for interrupted x30. */
    li t6, 31 /* Distinct expected value for interrupted x31. */
    li sp, 0                /* The vector must not store through this address. */
    ecall                   /* Cause 11 uses the established returning policy. */
    addi ra, ra, -1 /* Subtract only this register's own seed. */
    bnez ra, .Lumicom_trap_ecall_bad
    bnez sp, .Lumicom_trap_ecall_bad /* Must still be zero here. */
    addi gp, gp, -3 /* Subtract only this register's own seed. */
    bnez gp, .Lumicom_trap_ecall_bad
    addi tp, tp, -4 /* Subtract only this register's own seed. */
    bnez tp, .Lumicom_trap_ecall_bad
    addi t0, t0, -5 /* Subtract only this register's own seed. */
    bnez t0, .Lumicom_trap_ecall_bad
    addi t1, t1, -6 /* Subtract only this register's own seed. */
    bnez t1, .Lumicom_trap_ecall_bad
    addi t2, t2, -7 /* Subtract only this register's own seed. */
    bnez t2, .Lumicom_trap_ecall_bad
    addi s0, s0, -8 /* Subtract only this register's own seed. */
    bnez s0, .Lumicom_trap_ecall_bad
    addi s1, s1, -9 /* Subtract only this register's own seed. */
    bnez s1, .Lumicom_trap_ecall_bad
    addi a0, a0, -10 /* Subtract only this register's own seed. */
    bnez a0, .Lumicom_trap_ecall_bad
    addi a1, a1, -11 /* Subtract only this register's own seed. */
    bnez a1, .Lumicom_trap_ecall_bad
    addi a2, a2, -12 /* Subtract only this register's own seed. */
    bnez a2, .Lumicom_trap_ecall_bad
    addi a3, a3, -13 /* Subtract only this register's own seed. */
    bnez a3, .Lumicom_trap_ecall_bad
    addi a4, a4, -14 /* Subtract only this register's own seed. */
    bnez a4, .Lumicom_trap_ecall_bad
    addi a5, a5, -15 /* Subtract only this register's own seed. */
    bnez a5, .Lumicom_trap_ecall_bad
    addi a6, a6, -16 /* Subtract only this register's own seed. */
    bnez a6, .Lumicom_trap_ecall_bad
    addi a7, a7, -17 /* Subtract only this register's own seed. */
    bnez a7, .Lumicom_trap_ecall_bad
    addi s2, s2, -18 /* Subtract only this register's own seed. */
    bnez s2, .Lumicom_trap_ecall_bad
    addi s3, s3, -19 /* Subtract only this register's own seed. */
    bnez s3, .Lumicom_trap_ecall_bad
    addi s4, s4, -20 /* Subtract only this register's own seed. */
    bnez s4, .Lumicom_trap_ecall_bad
    addi s5, s5, -21 /* Subtract only this register's own seed. */
    bnez s5, .Lumicom_trap_ecall_bad
    addi s6, s6, -22 /* Subtract only this register's own seed. */
    bnez s6, .Lumicom_trap_ecall_bad
    addi s7, s7, -23 /* Subtract only this register's own seed. */
    bnez s7, .Lumicom_trap_ecall_bad
    addi s8, s8, -24 /* Subtract only this register's own seed. */
    bnez s8, .Lumicom_trap_ecall_bad
    addi s9, s9, -25 /* Subtract only this register's own seed. */
    bnez s9, .Lumicom_trap_ecall_bad
    addi s10, s10, -26 /* Subtract only this register's own seed. */
    bnez s10, .Lumicom_trap_ecall_bad
    addi s11, s11, -27 /* Subtract only this register's own seed. */
    bnez s11, .Lumicom_trap_ecall_bad
    addi t3, t3, -28 /* Subtract only this register's own seed. */
    bnez t3, .Lumicom_trap_ecall_bad
    addi t4, t4, -29 /* Subtract only this register's own seed. */
    bnez t4, .Lumicom_trap_ecall_bad
    addi t5, t5, -30 /* Subtract only this register's own seed. */
    bnez t5, .Lumicom_trap_ecall_bad
    addi t6, t6, -31 /* Subtract only this register's own seed. */
    bnez t6, .Lumicom_trap_ecall_bad
    li a0, 1                /* Every tested register survived the trap. */
    j .Lumicom_trap_ecall_restore
.Lumicom_trap_ecall_bad:
    li a0, 0                /* Report failure without using the poisoned sp. */
.Lumicom_trap_ecall_restore:
    csrr sp, sscratch        /* Return to the original valid machine stack. */
    ld t0, 120(sp)
    csrw sscratch, t0        /* Restore the caller's supervisor scratch value. */
    ld ra, 0(sp) /* Restore caller ra. */
    ld gp, 8(sp) /* Restore caller gp. */
    ld tp, 16(sp) /* Restore caller tp. */
    ld s0, 24(sp) /* Restore caller s0. */
    ld s1, 32(sp) /* Restore caller s1. */
    ld s2, 40(sp) /* Restore caller s2. */
    ld s3, 48(sp) /* Restore caller s3. */
    ld s4, 56(sp) /* Restore caller s4. */
    ld s5, 64(sp) /* Restore caller s5. */
    ld s6, 72(sp) /* Restore caller s6. */
    ld s7, 80(sp) /* Restore caller s7. */
    ld s8, 88(sp) /* Restore caller s8. */
    ld s9, 96(sp) /* Restore caller s9. */
    ld s10, 104(sp) /* Restore caller s10. */
    ld s11, 112(sp) /* Restore caller s11. */
    addi sp, sp, 128
    ret
    .size UmicomRiscvTrapRegistersProbe, . - UmicomRiscvTrapRegistersProbe

    .global UmicomRiscvTrapTimerRegistersProbe
    .type UmicomRiscvTrapTimerRegistersProbe, @function
UmicomRiscvTrapTimerRegistersProbe:
    addi sp, sp, -128        /* Preserve an aligned machine caller frame. */
    sd ra, 0(sp) /* Preserve caller ra. */
    sd gp, 8(sp) /* Preserve caller gp. */
    sd tp, 16(sp) /* Preserve caller tp. */
    sd s0, 24(sp) /* Preserve caller s0. */
    sd s1, 32(sp) /* Preserve caller s1. */
    sd s2, 40(sp) /* Preserve caller s2. */
    sd s3, 48(sp) /* Preserve caller s3. */
    sd s4, 56(sp) /* Preserve caller s4. */
    sd s5, 64(sp) /* Preserve caller s5. */
    sd s6, 72(sp) /* Preserve caller s6. */
    sd s7, 80(sp) /* Preserve caller s7. */
    sd s8, 88(sp) /* Preserve caller s8. */
    sd s9, 96(sp) /* Preserve caller s9. */
    sd s10, 104(sp) /* Preserve caller s10. */
    sd s11, 112(sp) /* Preserve caller s11. */
    csrr t0, sscratch
    sd t0, 120(sp)           /* Do not leave a borrowed supervisor CSR changed. */
    csrw sscratch, sp        /* Trusted caller stack survives the zero-sp test. */
    li ra, 1 /* Distinct expected value for interrupted x1. */
    li gp, 3 /* Distinct expected value for interrupted x3. */
    li tp, 4 /* Distinct expected value for interrupted x4. */
    li t0, 5 /* Distinct expected value for interrupted x5. */
    li t1, 6 /* Distinct expected value for interrupted x6. */
    li t2, 7 /* Distinct expected value for interrupted x7. */
    li s0, 8 /* Distinct expected value for interrupted x8. */
    li s1, 9 /* Distinct expected value for interrupted x9. */
    li a0, 10 /* Distinct expected value for interrupted x10. */
    li a1, 11 /* Distinct expected value for interrupted x11. */
    li a2, 12 /* Distinct expected value for interrupted x12. */
    li a3, 13 /* Distinct expected value for interrupted x13. */
    li a4, 14 /* Distinct expected value for interrupted x14. */
    li a5, 15 /* Distinct expected value for interrupted x15. */
    li a6, 16 /* Distinct expected value for interrupted x16. */
    li a7, 17 /* Distinct expected value for interrupted x17. */
    li s2, 18 /* Distinct expected value for interrupted x18. */
    li s3, 19 /* Distinct expected value for interrupted x19. */
    li s4, 20 /* Distinct expected value for interrupted x20. */
    li s5, 21 /* Distinct expected value for interrupted x21. */
    li s6, 22 /* Distinct expected value for interrupted x22. */
    li s7, 23 /* Distinct expected value for interrupted x23. */
    li s8, 24 /* Distinct expected value for interrupted x24. */
    li s9, 25 /* Distinct expected value for interrupted x25. */
    li s10, 26 /* Distinct expected value for interrupted x26. */
    li s11, 27 /* Distinct expected value for interrupted x27. */
    li t3, 28 /* Distinct expected value for interrupted x28. */
    li t4, 29 /* Distinct expected value for interrupted x29. */
    li t5, 30 /* Distinct expected value for interrupted x30. */
    li t6, 31 /* Distinct expected value for interrupted x31. */
    li sp, 0                /* The vector must not store through this address. */
    csrsi mstatus, 8        /* The caller already programmed MTIE and a deadline. */
.Lumicom_trap_timer_poll:
    csrr t6, mie             /* t6 is the poll scratch; all other GPRs stay seeded. */
    andi t6, t6, 0x80
    bnez t6, .Lumicom_trap_timer_poll
    addi ra, ra, -1 /* Subtract only this register's own seed. */
    bnez ra, .Lumicom_trap_timer_bad
    bnez sp, .Lumicom_trap_timer_bad /* Must still be zero here. */
    addi gp, gp, -3 /* Subtract only this register's own seed. */
    bnez gp, .Lumicom_trap_timer_bad
    addi tp, tp, -4 /* Subtract only this register's own seed. */
    bnez tp, .Lumicom_trap_timer_bad
    addi t0, t0, -5 /* Subtract only this register's own seed. */
    bnez t0, .Lumicom_trap_timer_bad
    addi t1, t1, -6 /* Subtract only this register's own seed. */
    bnez t1, .Lumicom_trap_timer_bad
    addi t2, t2, -7 /* Subtract only this register's own seed. */
    bnez t2, .Lumicom_trap_timer_bad
    addi s0, s0, -8 /* Subtract only this register's own seed. */
    bnez s0, .Lumicom_trap_timer_bad
    addi s1, s1, -9 /* Subtract only this register's own seed. */
    bnez s1, .Lumicom_trap_timer_bad
    addi a0, a0, -10 /* Subtract only this register's own seed. */
    bnez a0, .Lumicom_trap_timer_bad
    addi a1, a1, -11 /* Subtract only this register's own seed. */
    bnez a1, .Lumicom_trap_timer_bad
    addi a2, a2, -12 /* Subtract only this register's own seed. */
    bnez a2, .Lumicom_trap_timer_bad
    addi a3, a3, -13 /* Subtract only this register's own seed. */
    bnez a3, .Lumicom_trap_timer_bad
    addi a4, a4, -14 /* Subtract only this register's own seed. */
    bnez a4, .Lumicom_trap_timer_bad
    addi a5, a5, -15 /* Subtract only this register's own seed. */
    bnez a5, .Lumicom_trap_timer_bad
    addi a6, a6, -16 /* Subtract only this register's own seed. */
    bnez a6, .Lumicom_trap_timer_bad
    addi a7, a7, -17 /* Subtract only this register's own seed. */
    bnez a7, .Lumicom_trap_timer_bad
    addi s2, s2, -18 /* Subtract only this register's own seed. */
    bnez s2, .Lumicom_trap_timer_bad
    addi s3, s3, -19 /* Subtract only this register's own seed. */
    bnez s3, .Lumicom_trap_timer_bad
    addi s4, s4, -20 /* Subtract only this register's own seed. */
    bnez s4, .Lumicom_trap_timer_bad
    addi s5, s5, -21 /* Subtract only this register's own seed. */
    bnez s5, .Lumicom_trap_timer_bad
    addi s6, s6, -22 /* Subtract only this register's own seed. */
    bnez s6, .Lumicom_trap_timer_bad
    addi s7, s7, -23 /* Subtract only this register's own seed. */
    bnez s7, .Lumicom_trap_timer_bad
    addi s8, s8, -24 /* Subtract only this register's own seed. */
    bnez s8, .Lumicom_trap_timer_bad
    addi s9, s9, -25 /* Subtract only this register's own seed. */
    bnez s9, .Lumicom_trap_timer_bad
    addi s10, s10, -26 /* Subtract only this register's own seed. */
    bnez s10, .Lumicom_trap_timer_bad
    addi s11, s11, -27 /* Subtract only this register's own seed. */
    bnez s11, .Lumicom_trap_timer_bad
    addi t3, t3, -28 /* Subtract only this register's own seed. */
    bnez t3, .Lumicom_trap_timer_bad
    addi t4, t4, -29 /* Subtract only this register's own seed. */
    bnez t4, .Lumicom_trap_timer_bad
    addi t5, t5, -30 /* Subtract only this register's own seed. */
    bnez t5, .Lumicom_trap_timer_bad
    bnez t6, .Lumicom_trap_timer_bad /* Must still be zero here. */
    li a0, 1                /* Every tested register survived the trap. */
    j .Lumicom_trap_timer_restore
.Lumicom_trap_timer_bad:
    li a0, 0                /* Report failure without using the poisoned sp. */
.Lumicom_trap_timer_restore:
    csrr sp, sscratch        /* Return to the original valid machine stack. */
    ld t0, 120(sp)
    csrw sscratch, t0        /* Restore the caller's supervisor scratch value. */
    ld ra, 0(sp) /* Restore caller ra. */
    ld gp, 8(sp) /* Restore caller gp. */
    ld tp, 16(sp) /* Restore caller tp. */
    ld s0, 24(sp) /* Restore caller s0. */
    ld s1, 32(sp) /* Restore caller s1. */
    ld s2, 40(sp) /* Restore caller s2. */
    ld s3, 48(sp) /* Restore caller s3. */
    ld s4, 56(sp) /* Restore caller s4. */
    ld s5, 64(sp) /* Restore caller s5. */
    ld s6, 72(sp) /* Restore caller s6. */
    ld s7, 80(sp) /* Restore caller s7. */
    ld s8, 88(sp) /* Restore caller s8. */
    ld s9, 96(sp) /* Restore caller s9. */
    ld s10, 104(sp) /* Restore caller s10. */
    ld s11, 112(sp) /* Restore caller s11. */
    addi sp, sp, 128
    ret
    .size UmicomRiscvTrapTimerRegistersProbe, . - UmicomRiscvTrapTimerRegistersProbe

    /* Force four-byte memory instructions so their explicit recovery labels
     * are unambiguous even though the overall Kernel enables compressed code. */
    .option push
    .option norvc
    .global UmicomRiscvTrapLoadProbe
    .type UmicomRiscvTrapLoadProbe, @function
UmicomRiscvTrapLoadProbe:
    csrr t0, mstatus         /* Save the caller's actual machine access policy. */
    li t1, 0x21800
    not t1, t1
    and t1, t0, t1          /* Remove old MPP and MPRV from the temporary copy. */
    li t2, 0x20800           /* MPRV=1 and MPP=S for one data access only. */
    or t1, t1, t2
    csrw mstatus, t1
    .global UmicomRiscvTrapLoadInstruction
UmicomRiscvTrapLoadInstruction:
    ld a1, 0(a0)            /* The test selects an unmapped supervisor address. */
    .global UmicomRiscvTrapLoadResume
UmicomRiscvTrapLoadResume:
    csrw mstatus, t0         /* Cleanup runs only at this fixed instruction. */
    ret
    .size UmicomRiscvTrapLoadProbe, . - UmicomRiscvTrapLoadProbe

    .global UmicomRiscvTrapStoreProbe
    .type UmicomRiscvTrapStoreProbe, @function
UmicomRiscvTrapStoreProbe:
    csrr t0, mstatus
    li t1, 0x21800
    not t1, t1
    and t1, t0, t1
    li t2, 0x20800
    or t1, t1, t2
    csrw mstatus, t1
    .global UmicomRiscvTrapStoreInstruction
UmicomRiscvTrapStoreInstruction:
    sd a1, 0(a0)            /* The test selects a read-only supervisor page. */
    .global UmicomRiscvTrapStoreResume
UmicomRiscvTrapStoreResume:
    csrw mstatus, t0         /* The fault cannot leave MPRV live in the caller. */
    ret
    .size UmicomRiscvTrapStoreProbe, . - UmicomRiscvTrapStoreProbe

    .global UmicomRiscvTrapNestedProbe
    .type UmicomRiscvTrapNestedProbe, @function
UmicomRiscvTrapNestedProbe:
    .global UmicomRiscvTrapNestedEcall
UmicomRiscvTrapNestedEcall:
    ecall                   /* Only the separate test ELF injects a second fault. */
    ret
    .size UmicomRiscvTrapNestedProbe, . - UmicomRiscvTrapNestedProbe

    .global UmicomRiscvTrapNestedFault
    .type UmicomRiscvTrapNestedFault, @function
UmicomRiscvTrapNestedFault:
    .global UmicomRiscvTrapNestedInstruction
UmicomRiscvTrapNestedInstruction:
    .word 0                 /* Guaranteed illegal encoding on the tested profile. */
    ret                     /* Never reached; a valid opcode cannot replace the test. */
    .size UmicomRiscvTrapNestedFault, . - UmicomRiscvTrapNestedFault
    .option pop
