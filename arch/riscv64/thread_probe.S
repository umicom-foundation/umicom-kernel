/* Umicom Kernel register-preservation acceptance probe.
 * Seed every RV64 callee-saved integer register differently for each thread,
 * yield through the real scheduler, then compare before restoring the caller.
 * A C local alone might be spilled and miss a broken saved-register slot.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.umicom_thread_probe, "ax", @progbits
    .balign 16
    .global UmicomRiscvThreadRegistersProbe
    .type UmicomRiscvThreadRegistersProbe, @function
UmicomRiscvThreadRegistersProbe:
    /* a0=scheduler, a1=thread-specific seed. Keep the probe's caller intact. */
    addi sp, sp, -128
    sd ra, 0(sp) /* Returning from Yield must not lose this caller's ra. */
    sd s0, 8(sp) /* Preserve the caller's s0. */
    sd s1, 16(sp) /* Preserve the caller's s1. */
    sd s2, 24(sp) /* Preserve the caller's s2. */
    sd s3, 32(sp) /* Preserve the caller's s3. */
    sd s4, 40(sp) /* Preserve the caller's s4. */
    sd s5, 48(sp) /* Preserve the caller's s5. */
    sd s6, 56(sp) /* Preserve the caller's s6. */
    sd s7, 64(sp) /* Preserve the caller's s7. */
    sd s8, 72(sp) /* Preserve the caller's s8. */
    sd s9, 80(sp) /* Preserve the caller's s9. */
    sd s10, 88(sp) /* Preserve the caller's s10. */
    sd s11, 96(sp) /* Preserve the caller's s11. */
    sd a1, 104(sp) /* Expected seed stays on this thread's own stack. */
    sd gp, 112(sp) /* Also check that non-allocatable gp remains unchanged. */
    sd tp, 120(sp) /* The same rule applies to tp. */
    addi s0, a1, 1 /* Unique expected value for this register/thread. */
    addi s1, a1, 2 /* Unique expected value for this register/thread. */
    addi s2, a1, 3 /* Unique expected value for this register/thread. */
    addi s3, a1, 4 /* Unique expected value for this register/thread. */
    addi s4, a1, 5 /* Unique expected value for this register/thread. */
    addi s5, a1, 6 /* Unique expected value for this register/thread. */
    addi s6, a1, 7 /* Unique expected value for this register/thread. */
    addi s7, a1, 8 /* Unique expected value for this register/thread. */
    addi s8, a1, 9 /* Unique expected value for this register/thread. */
    addi s9, a1, 10 /* Unique expected value for this register/thread. */
    addi s10, a1, 11 /* Unique expected value for this register/thread. */
    addi s11, a1, 12 /* Unique expected value for this register/thread. */
    call UmicomKernelThreadYield /* Another thread overwrites the physical CPU registers. */
    bnez a0, .Lumicom_thread_probe_failed /* Yield must return success. */
    ld t0, 104(sp) /* Reload this thread's seed after restoring its stack. */
    addi t1, t0, 1
    bne s0, t1, .Lumicom_thread_probe_failed /* Detect a missing s0 save/restore. */
    addi t1, t0, 2
    bne s1, t1, .Lumicom_thread_probe_failed /* Detect a missing s1 save/restore. */
    addi t1, t0, 3
    bne s2, t1, .Lumicom_thread_probe_failed /* Detect a missing s2 save/restore. */
    addi t1, t0, 4
    bne s3, t1, .Lumicom_thread_probe_failed /* Detect a missing s3 save/restore. */
    addi t1, t0, 5
    bne s4, t1, .Lumicom_thread_probe_failed /* Detect a missing s4 save/restore. */
    addi t1, t0, 6
    bne s5, t1, .Lumicom_thread_probe_failed /* Detect a missing s5 save/restore. */
    addi t1, t0, 7
    bne s6, t1, .Lumicom_thread_probe_failed /* Detect a missing s6 save/restore. */
    addi t1, t0, 8
    bne s7, t1, .Lumicom_thread_probe_failed /* Detect a missing s7 save/restore. */
    addi t1, t0, 9
    bne s8, t1, .Lumicom_thread_probe_failed /* Detect a missing s8 save/restore. */
    addi t1, t0, 10
    bne s9, t1, .Lumicom_thread_probe_failed /* Detect a missing s9 save/restore. */
    addi t1, t0, 11
    bne s10, t1, .Lumicom_thread_probe_failed /* Detect a missing s10 save/restore. */
    addi t1, t0, 12
    bne s11, t1, .Lumicom_thread_probe_failed /* Detect a missing s11 save/restore. */
    ld t1, 112(sp)
    bne gp, t1, .Lumicom_thread_probe_failed
    ld t1, 120(sp)
    bne tp, t1, .Lumicom_thread_probe_failed
    li a0, 1 /* All expected values survived the cooperative suspension. */
    j .Lumicom_thread_probe_restore
.Lumicom_thread_probe_failed:
    li a0, 0 /* The C acceptance path turns this into a visible guest failure. */
.Lumicom_thread_probe_restore:
    ld ra, 0(sp) /* Return as an ordinary ABI-conforming function. */
    ld s0, 8(sp) /* Restore the original caller's s0. */
    ld s1, 16(sp) /* Restore the original caller's s1. */
    ld s2, 24(sp) /* Restore the original caller's s2. */
    ld s3, 32(sp) /* Restore the original caller's s3. */
    ld s4, 40(sp) /* Restore the original caller's s4. */
    ld s5, 48(sp) /* Restore the original caller's s5. */
    ld s6, 56(sp) /* Restore the original caller's s6. */
    ld s7, 64(sp) /* Restore the original caller's s7. */
    ld s8, 72(sp) /* Restore the original caller's s8. */
    ld s9, 80(sp) /* Restore the original caller's s9. */
    ld s10, 88(sp) /* Restore the original caller's s10. */
    ld s11, 96(sp) /* Restore the original caller's s11. */
    ld gp, 112(sp)
    ld tp, 120(sp)
    addi sp, sp, 128
    ret
    .size UmicomRiscvThreadRegistersProbe, . - UmicomRiscvThreadRegistersProbe
