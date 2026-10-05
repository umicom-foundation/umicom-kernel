/* Umicom Kernel asynchronous integer-state diagnostic.
 * The two instances below exchange the loop's scratch register. One holds t1
 * as a sentinel, the other t0. Together they check every mutable integer
 * register, not only the callee-saved subset needed by an ordinary C call.
 * Stack and progress-pointer checks complement the register sentinels.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.umicom_scheduled_registers, "ax", @progbits
    .balign 4

    /* This macro is test code, not a second production context switch. Reusing
     * its body makes the only difference explicit: which register does work
     * while the other registers carry distinct, task-specific sentinels. */
    .macro UmicomScheduledProbe name, scratch, index
    .global \name
    .type \name, @function
\name:
    addi sp, sp, -512 /* Own expected values and the real C caller's state. */
    sd ra, 0(sp) /* Preserve the C caller's ra. */
    sd gp, 8(sp) /* Preserve the C caller's gp. */
    sd tp, 16(sp) /* Preserve the C caller's tp. */
    sd s0, 24(sp) /* Preserve the C caller's s0. */
    sd s1, 32(sp) /* Preserve the C caller's s1. */
    sd s2, 40(sp) /* Preserve the C caller's s2. */
    sd s3, 48(sp) /* Preserve the C caller's s3. */
    sd s4, 56(sp) /* Preserve the C caller's s4. */
    sd s5, 64(sp) /* Preserve the C caller's s5. */
    sd s6, 72(sp) /* Preserve the C caller's s6. */
    sd s7, 80(sp) /* Preserve the C caller's s7. */
    sd s8, 88(sp) /* Preserve the C caller's s8. */
    sd s9, 96(sp) /* Preserve the C caller's s9. */
    sd s10, 104(sp) /* Preserve the C caller's s10. */
    sd s11, 112(sp) /* Preserve the C caller's s11. */
    sd a0, 120(sp) /* Original private observation pointer. */
    sd a1, 128(sp) /* Task-specific seed, not shared across address spaces. */
    sd sp, 144(sp) /* Exact live stack base, independently of saved register slots. */
    .if \index != 1
    ld \scratch, 128(sp)
    addi ra, \scratch, 17
    sd ra, 160(sp) /* Expected ra during asynchronous stops. */
    .endif
    .if \index != 3
    ld \scratch, 128(sp)
    addi gp, \scratch, 51
    sd gp, 176(sp) /* Expected gp during asynchronous stops. */
    .endif
    .if \index != 4
    ld \scratch, 128(sp)
    addi tp, \scratch, 68
    sd tp, 184(sp) /* Expected tp during asynchronous stops. */
    .endif
    .if \index != 5
    ld \scratch, 128(sp)
    addi t0, \scratch, 85
    sd t0, 192(sp) /* Expected t0 during asynchronous stops. */
    .endif
    .if \index != 6
    ld \scratch, 128(sp)
    addi t1, \scratch, 102
    sd t1, 200(sp) /* Expected t1 during asynchronous stops. */
    .endif
    .if \index != 8
    ld \scratch, 128(sp)
    addi s0, \scratch, 136
    sd s0, 216(sp) /* Expected s0 during asynchronous stops. */
    .endif
    .if \index != 9
    ld \scratch, 128(sp)
    addi s1, \scratch, 153
    sd s1, 224(sp) /* Expected s1 during asynchronous stops. */
    .endif
    .if \index != 10
    ld \scratch, 128(sp)
    addi a0, \scratch, 170
    sd a0, 232(sp) /* Expected a0 during asynchronous stops. */
    .endif
    .if \index != 11
    ld \scratch, 128(sp)
    addi a1, \scratch, 187
    sd a1, 240(sp) /* Expected a1 during asynchronous stops. */
    .endif
    .if \index != 12
    ld \scratch, 128(sp)
    addi a2, \scratch, 204
    sd a2, 248(sp) /* Expected a2 during asynchronous stops. */
    .endif
    .if \index != 13
    ld \scratch, 128(sp)
    addi a3, \scratch, 221
    sd a3, 256(sp) /* Expected a3 during asynchronous stops. */
    .endif
    .if \index != 14
    ld \scratch, 128(sp)
    addi a4, \scratch, 238
    sd a4, 264(sp) /* Expected a4 during asynchronous stops. */
    .endif
    .if \index != 15
    ld \scratch, 128(sp)
    addi a5, \scratch, 255
    sd a5, 272(sp) /* Expected a5 during asynchronous stops. */
    .endif
    .if \index != 16
    ld \scratch, 128(sp)
    addi a6, \scratch, 272
    sd a6, 280(sp) /* Expected a6 during asynchronous stops. */
    .endif
    .if \index != 17
    ld \scratch, 128(sp)
    addi a7, \scratch, 289
    sd a7, 288(sp) /* Expected a7 during asynchronous stops. */
    .endif
    .if \index != 18
    ld \scratch, 128(sp)
    addi s2, \scratch, 306
    sd s2, 296(sp) /* Expected s2 during asynchronous stops. */
    .endif
    .if \index != 19
    ld \scratch, 128(sp)
    addi s3, \scratch, 323
    sd s3, 304(sp) /* Expected s3 during asynchronous stops. */
    .endif
    .if \index != 20
    ld \scratch, 128(sp)
    addi s4, \scratch, 340
    sd s4, 312(sp) /* Expected s4 during asynchronous stops. */
    .endif
    .if \index != 21
    ld \scratch, 128(sp)
    addi s5, \scratch, 357
    sd s5, 320(sp) /* Expected s5 during asynchronous stops. */
    .endif
    .if \index != 22
    ld \scratch, 128(sp)
    addi s6, \scratch, 374
    sd s6, 328(sp) /* Expected s6 during asynchronous stops. */
    .endif
    .if \index != 23
    ld \scratch, 128(sp)
    addi s7, \scratch, 391
    sd s7, 336(sp) /* Expected s7 during asynchronous stops. */
    .endif
    .if \index != 24
    ld \scratch, 128(sp)
    addi s8, \scratch, 408
    sd s8, 344(sp) /* Expected s8 during asynchronous stops. */
    .endif
    .if \index != 25
    ld \scratch, 128(sp)
    addi s9, \scratch, 425
    sd s9, 352(sp) /* Expected s9 during asynchronous stops. */
    .endif
    .if \index != 26
    ld \scratch, 128(sp)
    addi s10, \scratch, 442
    sd s10, 360(sp) /* Expected s10 during asynchronous stops. */
    .endif
    .if \index != 27
    ld \scratch, 128(sp)
    addi s11, \scratch, 459
    sd s11, 368(sp) /* Expected s11 during asynchronous stops. */
    .endif
    .if \index != 28
    ld \scratch, 128(sp)
    addi t3, \scratch, 476
    sd t3, 376(sp) /* Expected t3 during asynchronous stops. */
    .endif
    .if \index != 29
    ld \scratch, 128(sp)
    addi t4, \scratch, 493
    sd t4, 384(sp) /* Expected t4 during asynchronous stops. */
    .endif
    .if \index != 30
    ld \scratch, 128(sp)
    addi t5, \scratch, 510
    sd t5, 392(sp) /* Expected t5 during asynchronous stops. */
    .endif
    .if \index != 31
    ld \scratch, 128(sp)
    addi t6, \scratch, 527
    sd t6, 400(sp) /* Expected t6 during asynchronous stops. */
    .endif
    ld t2, 120(sp) /* This pointer remains stable throughout the busy loop. */
.L\name\()_spin:
    ld \scratch, 8(t2)
    addi \scratch, \scratch, 1
    sd \scratch, 8(t2)
    ld \scratch, 24(t2)
    beqz \scratch, .L\name\()_spin
    ld \scratch, 144(sp)
    bne sp, \scratch, .L\name\()_bad
    ld \scratch, 120(sp)
    bne t2, \scratch, .L\name\()_bad
    .if \index != 1
    ld \scratch, 160(sp)
    bne ra, \scratch, .L\name\()_bad /* Detect lost ra even if C would spill it. */
    .endif
    .if \index != 3
    ld \scratch, 176(sp)
    bne gp, \scratch, .L\name\()_bad /* Detect lost gp even if C would spill it. */
    .endif
    .if \index != 4
    ld \scratch, 184(sp)
    bne tp, \scratch, .L\name\()_bad /* Detect lost tp even if C would spill it. */
    .endif
    .if \index != 5
    ld \scratch, 192(sp)
    bne t0, \scratch, .L\name\()_bad /* Detect lost t0 even if C would spill it. */
    .endif
    .if \index != 6
    ld \scratch, 200(sp)
    bne t1, \scratch, .L\name\()_bad /* Detect lost t1 even if C would spill it. */
    .endif
    .if \index != 8
    ld \scratch, 216(sp)
    bne s0, \scratch, .L\name\()_bad /* Detect lost s0 even if C would spill it. */
    .endif
    .if \index != 9
    ld \scratch, 224(sp)
    bne s1, \scratch, .L\name\()_bad /* Detect lost s1 even if C would spill it. */
    .endif
    .if \index != 10
    ld \scratch, 232(sp)
    bne a0, \scratch, .L\name\()_bad /* Detect lost a0 even if C would spill it. */
    .endif
    .if \index != 11
    ld \scratch, 240(sp)
    bne a1, \scratch, .L\name\()_bad /* Detect lost a1 even if C would spill it. */
    .endif
    .if \index != 12
    ld \scratch, 248(sp)
    bne a2, \scratch, .L\name\()_bad /* Detect lost a2 even if C would spill it. */
    .endif
    .if \index != 13
    ld \scratch, 256(sp)
    bne a3, \scratch, .L\name\()_bad /* Detect lost a3 even if C would spill it. */
    .endif
    .if \index != 14
    ld \scratch, 264(sp)
    bne a4, \scratch, .L\name\()_bad /* Detect lost a4 even if C would spill it. */
    .endif
    .if \index != 15
    ld \scratch, 272(sp)
    bne a5, \scratch, .L\name\()_bad /* Detect lost a5 even if C would spill it. */
    .endif
    .if \index != 16
    ld \scratch, 280(sp)
    bne a6, \scratch, .L\name\()_bad /* Detect lost a6 even if C would spill it. */
    .endif
    .if \index != 17
    ld \scratch, 288(sp)
    bne a7, \scratch, .L\name\()_bad /* Detect lost a7 even if C would spill it. */
    .endif
    .if \index != 18
    ld \scratch, 296(sp)
    bne s2, \scratch, .L\name\()_bad /* Detect lost s2 even if C would spill it. */
    .endif
    .if \index != 19
    ld \scratch, 304(sp)
    bne s3, \scratch, .L\name\()_bad /* Detect lost s3 even if C would spill it. */
    .endif
    .if \index != 20
    ld \scratch, 312(sp)
    bne s4, \scratch, .L\name\()_bad /* Detect lost s4 even if C would spill it. */
    .endif
    .if \index != 21
    ld \scratch, 320(sp)
    bne s5, \scratch, .L\name\()_bad /* Detect lost s5 even if C would spill it. */
    .endif
    .if \index != 22
    ld \scratch, 328(sp)
    bne s6, \scratch, .L\name\()_bad /* Detect lost s6 even if C would spill it. */
    .endif
    .if \index != 23
    ld \scratch, 336(sp)
    bne s7, \scratch, .L\name\()_bad /* Detect lost s7 even if C would spill it. */
    .endif
    .if \index != 24
    ld \scratch, 344(sp)
    bne s8, \scratch, .L\name\()_bad /* Detect lost s8 even if C would spill it. */
    .endif
    .if \index != 25
    ld \scratch, 352(sp)
    bne s9, \scratch, .L\name\()_bad /* Detect lost s9 even if C would spill it. */
    .endif
    .if \index != 26
    ld \scratch, 360(sp)
    bne s10, \scratch, .L\name\()_bad /* Detect lost s10 even if C would spill it. */
    .endif
    .if \index != 27
    ld \scratch, 368(sp)
    bne s11, \scratch, .L\name\()_bad /* Detect lost s11 even if C would spill it. */
    .endif
    .if \index != 28
    ld \scratch, 376(sp)
    bne t3, \scratch, .L\name\()_bad /* Detect lost t3 even if C would spill it. */
    .endif
    .if \index != 29
    ld \scratch, 384(sp)
    bne t4, \scratch, .L\name\()_bad /* Detect lost t4 even if C would spill it. */
    .endif
    .if \index != 30
    ld \scratch, 392(sp)
    bne t5, \scratch, .L\name\()_bad /* Detect lost t5 even if C would spill it. */
    .endif
    .if \index != 31
    ld \scratch, 400(sp)
    bne t6, \scratch, .L\name\()_bad /* Detect lost t6 even if C would spill it. */
    .endif
    li a0, 0
    j .L\name\()_restore
.L\name\()_bad:
    li a0, 0xef20 /* Report an incorrect register, not a fabricated pass. */
.L\name\()_restore:
    ld ra, 0(sp) /* Restore the real C caller's ra. */
    ld gp, 8(sp) /* Restore the real C caller's gp. */
    ld tp, 16(sp) /* Restore the real C caller's tp. */
    ld s0, 24(sp) /* Restore the real C caller's s0. */
    ld s1, 32(sp) /* Restore the real C caller's s1. */
    ld s2, 40(sp) /* Restore the real C caller's s2. */
    ld s3, 48(sp) /* Restore the real C caller's s3. */
    ld s4, 56(sp) /* Restore the real C caller's s4. */
    ld s5, 64(sp) /* Restore the real C caller's s5. */
    ld s6, 72(sp) /* Restore the real C caller's s6. */
    ld s7, 80(sp) /* Restore the real C caller's s7. */
    ld s8, 88(sp) /* Restore the real C caller's s8. */
    ld s9, 96(sp) /* Restore the real C caller's s9. */
    ld s10, 104(sp) /* Restore the real C caller's s10. */
    ld s11, 112(sp) /* Restore the real C caller's s11. */
    addi sp, sp, 512
    ret
    .size \name, . - \name
    .endm

    UmicomScheduledProbe UmicomScheduledRegisterLoop, t0, 5
    UmicomScheduledProbe UmicomScheduledTemporaryRegisterLoop, t1, 6
