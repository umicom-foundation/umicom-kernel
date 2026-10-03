/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/user_execution.S
 *
 * PURPOSE:
 *   Enter U-mode, preserve user registers across resumable environment calls,
 *   and return to the machine caller on exit, fault or a timer deadline.
 *
 * EDUCATIONAL OVERVIEW:
 *   The first instruction of the private trap vector exchanges sp and
 *   mscratch. No memory operation trusts the user's stack. The complete user
 *   integer frame lives above a saved machine context; C dispatch runs below
 *   that context on the already valid machine stack.
 *
 *   The supervisor experiment remains independent and unchanged. Its handler
 *   deliberately returns on every trap; this monitor must instead resume
 *   successful system calls while keeping the saved machine return state.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/user_execution.h"

    /* This section is never included in the user page table. */
    .section .text.umicom_user_monitor, "ax", @progbits
    .balign 16
    .equ UMICOM_USER_CONTEXT_BYTES, 544 /* 256 machine bytes plus 288 user bytes. */
    .equ UMICOM_USER_FRAME_BASE, 256
    .equ UMICOM_USER_SAVED_RA, 0 /* Machine caller ra. */
    .equ UMICOM_USER_SAVED_S0, 8 /* Machine caller s0. */
    .equ UMICOM_USER_SAVED_S1, 16 /* Machine caller s1. */
    .equ UMICOM_USER_SAVED_S2, 24 /* Machine caller s2. */
    .equ UMICOM_USER_SAVED_S3, 32 /* Machine caller s3. */
    .equ UMICOM_USER_SAVED_S4, 40 /* Machine caller s4. */
    .equ UMICOM_USER_SAVED_S5, 48 /* Machine caller s5. */
    .equ UMICOM_USER_SAVED_S6, 56 /* Machine caller s6. */
    .equ UMICOM_USER_SAVED_S7, 64 /* Machine caller s7. */
    .equ UMICOM_USER_SAVED_S8, 72 /* Machine caller s8. */
    .equ UMICOM_USER_SAVED_S9, 80 /* Machine caller s9. */
    .equ UMICOM_USER_SAVED_S10, 88 /* Machine caller s10. */
    .equ UMICOM_USER_SAVED_S11, 96 /* Machine caller s11. */
    .equ UMICOM_USER_SAVED_GP, 104 /* Machine caller gp. */
    .equ UMICOM_USER_SAVED_TP, 112 /* Machine caller tp. */
    .equ UMICOM_USER_SAVED_MSTATUS, 120 /* Machine caller mstatus. */
    .equ UMICOM_USER_SAVED_MIE, 128 /* Machine caller mie. */
    .equ UMICOM_USER_SAVED_MTVEC, 136 /* Machine caller mtvec. */
    .equ UMICOM_USER_SAVED_MSCRATCH, 144 /* Machine caller mscratch. */
    .equ UMICOM_USER_SAVED_MEDELEG, 152 /* Machine caller medeleg. */
    .equ UMICOM_USER_SAVED_MIDELEG, 160 /* Machine caller mideleg. */
    .equ UMICOM_USER_SAVED_SATP, 168 /* Machine caller satp. */
    .equ UMICOM_USER_SAVED_PMPCFG0, 176 /* Machine caller pmpcfg0. */
    .equ UMICOM_USER_SAVED_PMPADDR0, 184 /* Machine caller pmpaddr0. */
    .equ UMICOM_USER_SAVED_MEPC, 192 /* Machine caller mepc. */
    .equ UMICOM_USER_SAVED_MCAUSE, 200 /* Machine caller mcause. */
    .equ UMICOM_USER_SAVED_MTVAL, 208 /* Machine caller mtval. */
    .equ UMICOM_USER_SAVED_MCOUNTEREN, 216 /* Machine caller mcounteren. */
    .equ UMICOM_USER_SAVED_SCOUNTEREN, 224 /* Machine caller scounteren. */
    .equ UMICOM_USER_SAVED_REQUEST, 232
    .equ UMICOM_USER_SAVED_SESSION, 240
    .equ UMICOM_USER_SAVED_RESULT, 248

    .global UmicomRiscvUserExecute
    .type UmicomRiscvUserExecute, @function
UmicomRiscvUserExecute:
    /* Entry is machine-only, with MPRV clear and interrupts already disabled.
     * Reserve both frames together so their offsets stay fixed across calls. */
    addi sp, sp, -UMICOM_USER_CONTEXT_BYTES
    sd ra, UMICOM_USER_SAVED_RA(sp) /* Preserve the C caller's ra. */
    sd s0, UMICOM_USER_SAVED_S0(sp) /* Preserve the C caller's s0. */
    sd s1, UMICOM_USER_SAVED_S1(sp) /* Preserve the C caller's s1. */
    sd s2, UMICOM_USER_SAVED_S2(sp) /* Preserve the C caller's s2. */
    sd s3, UMICOM_USER_SAVED_S3(sp) /* Preserve the C caller's s3. */
    sd s4, UMICOM_USER_SAVED_S4(sp) /* Preserve the C caller's s4. */
    sd s5, UMICOM_USER_SAVED_S5(sp) /* Preserve the C caller's s5. */
    sd s6, UMICOM_USER_SAVED_S6(sp) /* Preserve the C caller's s6. */
    sd s7, UMICOM_USER_SAVED_S7(sp) /* Preserve the C caller's s7. */
    sd s8, UMICOM_USER_SAVED_S8(sp) /* Preserve the C caller's s8. */
    sd s9, UMICOM_USER_SAVED_S9(sp) /* Preserve the C caller's s9. */
    sd s10, UMICOM_USER_SAVED_S10(sp) /* Preserve the C caller's s10. */
    sd s11, UMICOM_USER_SAVED_S11(sp) /* Preserve the C caller's s11. */
    sd gp, UMICOM_USER_SAVED_GP(sp) /* Preserve the C caller's gp. */
    sd tp, UMICOM_USER_SAVED_TP(sp) /* Preserve the C caller's tp. */
    sd a0, UMICOM_USER_SAVED_REQUEST(sp) /* Trusted Kernel request. */
    sd a1, UMICOM_USER_SAVED_SESSION(sp) /* Trusted Kernel session. */
    csrr t0, mstatus /* Read the caller-owned mstatus. */
    sd t0, UMICOM_USER_SAVED_MSTATUS(sp) /* Save for exact restoration. */
    csrr t0, mie /* Read the caller-owned mie. */
    sd t0, UMICOM_USER_SAVED_MIE(sp) /* Save for exact restoration. */
    csrr t0, mtvec /* Read the caller-owned mtvec. */
    sd t0, UMICOM_USER_SAVED_MTVEC(sp) /* Save for exact restoration. */
    csrr t0, mscratch /* Read the caller-owned mscratch. */
    sd t0, UMICOM_USER_SAVED_MSCRATCH(sp) /* Save for exact restoration. */
    csrr t0, medeleg /* Read the caller-owned medeleg. */
    sd t0, UMICOM_USER_SAVED_MEDELEG(sp) /* Save for exact restoration. */
    csrr t0, mideleg /* Read the caller-owned mideleg. */
    sd t0, UMICOM_USER_SAVED_MIDELEG(sp) /* Save for exact restoration. */
    csrr t0, satp /* Read the caller-owned satp. */
    sd t0, UMICOM_USER_SAVED_SATP(sp) /* Save for exact restoration. */
    csrr t0, pmpcfg0 /* Read the caller-owned pmpcfg0. */
    sd t0, UMICOM_USER_SAVED_PMPCFG0(sp) /* Save for exact restoration. */
    csrr t0, pmpaddr0 /* Read the caller-owned pmpaddr0. */
    sd t0, UMICOM_USER_SAVED_PMPADDR0(sp) /* Save for exact restoration. */
    csrr t0, mepc /* Read the caller-owned mepc. */
    sd t0, UMICOM_USER_SAVED_MEPC(sp) /* Save for exact restoration. */
    csrr t0, mcause /* Read the caller-owned mcause. */
    sd t0, UMICOM_USER_SAVED_MCAUSE(sp) /* Save for exact restoration. */
    csrr t0, mtval /* Read the caller-owned mtval. */
    sd t0, UMICOM_USER_SAVED_MTVAL(sp) /* Save for exact restoration. */
    csrr t0, mcounteren /* Read the caller-owned mcounteren. */
    sd t0, UMICOM_USER_SAVED_MCOUNTEREN(sp) /* Save for exact restoration. */
    csrr t0, scounteren /* Read the caller-owned scounteren. */
    sd t0, UMICOM_USER_SAVED_SCOUNTEREN(sp) /* Save for exact restoration. */
    li t0, 1 /* Refused until at least one user event is captured. */
    sd t0, UMICOM_USER_SAVED_RESULT(sp)

    /* This early monitor may borrow only an otherwise unused PMP bank. It
     * must not weaken an existing protection policy to make a test pass. */
    ld t0, UMICOM_USER_SAVED_PMPCFG0(sp)
    bnez t0, .Lumicom_user_restore
    ld t0, UMICOM_USER_SAVED_MIE(sp)
    bnez t0, .Lumicom_user_restore /* Caller must have no enabled sources. */
    ld t0, UMICOM_USER_SAVED_MSTATUS(sp)
    li t1, 0x20008 /* MPRV or global machine interrupt enable. */
    and t1, t0, t1
    bnez t1, .Lumicom_user_restore
    srli t1, t0, 32 /* UXL must select RV64, not an RV32 user ABI. */
    andi t1, t1, 3
    li t2, 2
    bne t1, t2, .Lumicom_user_restore

    /* Capture all user traps in our private vector. No supervisor exception
     * handler or delegated interrupt is part of this monitor. */
    csrw mie, zero
    csrw mscratch, sp
    la t0, UmicomRiscvUserTrapEntry
    csrw mtvec, t0
    csrw medeleg, zero
    csrw mideleg, zero
    csrw mcounteren, zero /* Do not expose machine performance counters. */
    csrw scounteren, zero /* U-mode cannot bypass the monitor for timing data. */

    /* Temporary PMP allows only the known RAM region, not UART/timer MMIO.
     * Page permissions then restrict each user to its explicit leaf mappings. */
    ld t3, UMICOM_USER_SAVED_REQUEST(sp)
    ld t0, UMICOM_USER_REQUEST_PMP(t3)
    csrw pmpaddr0, t0
    li t0, 0x1f /* Unlocked NAPOT RAM region, R/W/X. */
    csrw pmpcfg0, t0
    ld t0, UMICOM_USER_REQUEST_ROOT(t3)
    srli t0, t0, 12
    li t1, 8
    slli t1, t1, 60 /* Sv39, ASID zero; fence every root switch. */
    or t0, t0, t1
    csrw satp, t0
    sfence.vma zero, zero
    csrr t1, satp
    bne t0, t1, .Lumicom_user_restore
    ld t0, UMICOM_USER_REQUEST_ENTRY(t3)
    csrw mepc, t0
    ld t4, UMICOM_USER_REQUEST_STACK(t3)
    ld a0, UMICOM_USER_REQUEST_ARGUMENT(t3)

    /* Clear MPP to select U-mode; disable floating/vector state and privilege
     * overrides. Only integer state is supported by this trap-frame contract. */
    csrr t0, mstatus
    li t1, 0x7e7faa /* MPP, MPRV, SUM, MXR, FS/VS, SPP, interrupt/control bits. */
    not t1, t1
    and t0, t0, t1
    csrw mstatus, t0
    li t0, 0x80 /* Machine timer can recover control even from a busy U loop. */
    csrw mie, t0
    mv sp, t4 /* This value was checked against the writable user stack mapping. */

    /* Do not leak machine pointers through leftover integer registers. The
     * only nonzero incoming values are the user stack and operation argument. */
    li ra, 0 /* User starts with no inherited ra value. */
    li gp, 0 /* User starts with no inherited gp value. */
    li tp, 0 /* User starts with no inherited tp value. */
    li t0, 0 /* User starts with no inherited t0 value. */
    li t1, 0 /* User starts with no inherited t1 value. */
    li t2, 0 /* User starts with no inherited t2 value. */
    li s0, 0 /* User starts with no inherited s0 value. */
    li s1, 0 /* User starts with no inherited s1 value. */
    li a1, 0 /* User starts with no inherited a1 value. */
    li a2, 0 /* User starts with no inherited a2 value. */
    li a3, 0 /* User starts with no inherited a3 value. */
    li a4, 0 /* User starts with no inherited a4 value. */
    li a5, 0 /* User starts with no inherited a5 value. */
    li a6, 0 /* User starts with no inherited a6 value. */
    li a7, 0 /* User starts with no inherited a7 value. */
    li s2, 0 /* User starts with no inherited s2 value. */
    li s3, 0 /* User starts with no inherited s3 value. */
    li s4, 0 /* User starts with no inherited s4 value. */
    li s5, 0 /* User starts with no inherited s5 value. */
    li s6, 0 /* User starts with no inherited s6 value. */
    li s7, 0 /* User starts with no inherited s7 value. */
    li s8, 0 /* User starts with no inherited s8 value. */
    li s9, 0 /* User starts with no inherited s9 value. */
    li s10, 0 /* User starts with no inherited s10 value. */
    li s11, 0 /* User starts with no inherited s11 value. */
    li t3, 0 /* User starts with no inherited t3 value. */
    li t4, 0 /* User starts with no inherited t4 value. */
    li t5, 0 /* User starts with no inherited t5 value. */
    li t6, 0 /* User starts with no inherited t6 value. */
    mret /* Actual privilege becomes U; fetch, loads and stores use Sv39. */

    .balign 16
    .global UmicomRiscvUserTrapEntry
    .type UmicomRiscvUserTrapEntry, @function
UmicomRiscvUserTrapEntry:
    /* Even a poisoned/unmapped user sp cannot select the machine save area. */
    csrrw sp, mscratch, sp
    sd ra, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X1_RA(sp) /* Capture user ra. */
    sd gp, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X3_GP(sp) /* Capture user gp. */
    sd tp, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X4_TP(sp) /* Capture user tp. */
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X5_T0(sp) /* Capture user t0. */
    sd t1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X6_T1(sp) /* Capture user t1. */
    sd t2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X7_T2(sp) /* Capture user t2. */
    sd s0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X8_S0(sp) /* Capture user s0. */
    sd s1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X9_S1(sp) /* Capture user s1. */
    sd a0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X10_A0(sp) /* Capture user a0. */
    sd a1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X11_A1(sp) /* Capture user a1. */
    sd a2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X12_A2(sp) /* Capture user a2. */
    sd a3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X13_A3(sp) /* Capture user a3. */
    sd a4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X14_A4(sp) /* Capture user a4. */
    sd a5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X15_A5(sp) /* Capture user a5. */
    sd a6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X16_A6(sp) /* Capture user a6. */
    sd a7, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X17_A7(sp) /* Capture user a7. */
    sd s2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X18_S2(sp) /* Capture user s2. */
    sd s3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X19_S3(sp) /* Capture user s3. */
    sd s4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X20_S4(sp) /* Capture user s4. */
    sd s5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X21_S5(sp) /* Capture user s5. */
    sd s6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X22_S6(sp) /* Capture user s6. */
    sd s7, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X23_S7(sp) /* Capture user s7. */
    sd s8, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X24_S8(sp) /* Capture user s8. */
    sd s9, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X25_S9(sp) /* Capture user s9. */
    sd s10, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X26_S10(sp) /* Capture user s10. */
    sd s11, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X27_S11(sp) /* Capture user s11. */
    sd t3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X28_T3(sp) /* Capture user t3. */
    sd t4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X29_T4(sp) /* Capture user t4. */
    sd t5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X30_T5(sp) /* Capture user t5. */
    sd t6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X31_T6(sp) /* Capture user t6. */
    csrr t0, mscratch /* Recover the user's untrusted sp as a value only. */
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X2_SP(sp)
    csrr t0, mepc
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MEPC(sp) /* Capture mepc. */
    csrr t0, mstatus
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MSTATUS(sp) /* Capture mstatus. */
    csrr t0, mcause
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MCAUSE(sp) /* Capture mcause. */
    csrr t0, mtval
    sd t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MTVAL(sp) /* Capture mtval. */
    sd zero, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_RESERVED(sp)
    li t0, 0x20000
    csrc mstatus, t0 /* Diagnostics and copies must not inherit MPRV. */

    /* During the C dispatcher a Kernel fault belongs to the existing fatal
     * trap handler, not this user save path. A nested Kernel fault must never
     * overwrite the saved user frame or exchange sp with a user-controlled value. */
    ld t0, UMICOM_USER_SAVED_MTVEC(sp)
    csrw mtvec, t0
    ld a0, UMICOM_USER_SAVED_SESSION(sp)
    addi a1, sp, UMICOM_USER_FRAME_BASE
    call UmicomKernelUserTrapDispatch
    beqz a0, .Lumicom_user_terminal

    /* C has advanced only a validated ECALL PC and filled its result register.
     * Restore the private vector before letting user execution continue. */
    la t0, UmicomRiscvUserTrapEntry
    csrw mtvec, t0
    csrw mscratch, sp
    ld t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MEPC(sp)
    csrw mepc, t0
    ld t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_MSTATUS(sp)
    li t1, 0x7e7faa
    not t1, t1
    and t0, t0, t1 /* Keep U-mode and integer-only state on every resumption. */
    csrw mstatus, t0
    ld ra, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X1_RA(sp) /* Restore user ra. */
    ld gp, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X3_GP(sp) /* Restore user gp. */
    ld tp, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X4_TP(sp) /* Restore user tp. */
    ld t1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X6_T1(sp) /* Restore user t1. */
    ld t2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X7_T2(sp) /* Restore user t2. */
    ld s0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X8_S0(sp) /* Restore user s0. */
    ld s1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X9_S1(sp) /* Restore user s1. */
    ld a0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X10_A0(sp) /* Restore user a0. */
    ld a1, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X11_A1(sp) /* Restore user a1. */
    ld a2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X12_A2(sp) /* Restore user a2. */
    ld a3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X13_A3(sp) /* Restore user a3. */
    ld a4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X14_A4(sp) /* Restore user a4. */
    ld a5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X15_A5(sp) /* Restore user a5. */
    ld a6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X16_A6(sp) /* Restore user a6. */
    ld a7, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X17_A7(sp) /* Restore user a7. */
    ld s2, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X18_S2(sp) /* Restore user s2. */
    ld s3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X19_S3(sp) /* Restore user s3. */
    ld s4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X20_S4(sp) /* Restore user s4. */
    ld s5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X21_S5(sp) /* Restore user s5. */
    ld s6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X22_S6(sp) /* Restore user s6. */
    ld s7, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X23_S7(sp) /* Restore user s7. */
    ld s8, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X24_S8(sp) /* Restore user s8. */
    ld s9, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X25_S9(sp) /* Restore user s9. */
    ld s10, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X26_S10(sp) /* Restore user s10. */
    ld s11, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X27_S11(sp) /* Restore user s11. */
    ld t3, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X28_T3(sp) /* Restore user t3. */
    ld t4, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X29_T4(sp) /* Restore user t4. */
    ld t5, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X30_T5(sp) /* Restore user t5. */
    ld t6, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X31_T6(sp) /* Restore user t6. */
    ld t0, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X5_T0(sp)
    ld sp, UMICOM_USER_FRAME_BASE+UMICOM_USER_FRAME_X2_SP(sp) /* Last machine-frame read. */
    mret

.Lumicom_user_terminal:
    sd zero, UMICOM_USER_SAVED_RESULT(sp) /* Captured exit/fault/deadline, not automatic success. */
.Lumicom_user_restore:
    /* Restore the borrowed control state before its page tables can be freed. */
    csrw mie, zero /* Mask the temporary deadline source during restoration. */
    ld t0, UMICOM_USER_SAVED_SATP(sp)
    csrw satp, t0
    sfence.vma zero, zero
    csrw pmpcfg0, zero
    ld t0, UMICOM_USER_SAVED_PMPADDR0(sp)
    csrw pmpaddr0, t0
    ld t0, UMICOM_USER_SAVED_PMPCFG0(sp)
    csrw pmpcfg0, t0
    sfence.vma zero, zero
    ld t0, UMICOM_USER_SAVED_MEDELEG(sp)
    csrw medeleg, t0 /* Restore the machine caller's medeleg. */
    ld t0, UMICOM_USER_SAVED_MIDELEG(sp)
    csrw mideleg, t0 /* Restore the machine caller's mideleg. */
    ld t0, UMICOM_USER_SAVED_MTVEC(sp)
    csrw mtvec, t0 /* Restore the machine caller's mtvec. */
    ld t0, UMICOM_USER_SAVED_MSCRATCH(sp)
    csrw mscratch, t0 /* Restore the machine caller's mscratch. */
    ld t0, UMICOM_USER_SAVED_MEPC(sp)
    csrw mepc, t0 /* Restore the machine caller's mepc. */
    ld t0, UMICOM_USER_SAVED_MCAUSE(sp)
    csrw mcause, t0 /* Restore the machine caller's mcause. */
    ld t0, UMICOM_USER_SAVED_MTVAL(sp)
    csrw mtval, t0 /* Restore the machine caller's mtval. */
    ld t0, UMICOM_USER_SAVED_MCOUNTEREN(sp)
    csrw mcounteren, t0 /* Restore the machine caller's mcounteren. */
    ld t0, UMICOM_USER_SAVED_SCOUNTEREN(sp)
    csrw scounteren, t0 /* Restore the machine caller's scounteren. */
    ld t0, UMICOM_USER_SAVED_MIE(sp)
    csrw mie, t0 /* Restore the machine caller's mie. */
    ld a0, UMICOM_USER_SAVED_RESULT(sp)
    ld ra, UMICOM_USER_SAVED_RA(sp) /* Restore the C caller's ra. */
    ld s0, UMICOM_USER_SAVED_S0(sp) /* Restore the C caller's s0. */
    ld s1, UMICOM_USER_SAVED_S1(sp) /* Restore the C caller's s1. */
    ld s2, UMICOM_USER_SAVED_S2(sp) /* Restore the C caller's s2. */
    ld s3, UMICOM_USER_SAVED_S3(sp) /* Restore the C caller's s3. */
    ld s4, UMICOM_USER_SAVED_S4(sp) /* Restore the C caller's s4. */
    ld s5, UMICOM_USER_SAVED_S5(sp) /* Restore the C caller's s5. */
    ld s6, UMICOM_USER_SAVED_S6(sp) /* Restore the C caller's s6. */
    ld s7, UMICOM_USER_SAVED_S7(sp) /* Restore the C caller's s7. */
    ld s8, UMICOM_USER_SAVED_S8(sp) /* Restore the C caller's s8. */
    ld s9, UMICOM_USER_SAVED_S9(sp) /* Restore the C caller's s9. */
    ld s10, UMICOM_USER_SAVED_S10(sp) /* Restore the C caller's s10. */
    ld s11, UMICOM_USER_SAVED_S11(sp) /* Restore the C caller's s11. */
    ld gp, UMICOM_USER_SAVED_GP(sp) /* Restore the C caller's gp. */
    ld tp, UMICOM_USER_SAVED_TP(sp) /* Restore the C caller's tp. */
    ld t0, UMICOM_USER_SAVED_MSTATUS(sp)
    csrw mstatus, t0 /* Global interrupt state is restored last. */
    addi sp, sp, UMICOM_USER_CONTEXT_BYTES
    ret /* Back to the suspended machine C call, not to a failed user instruction. */
    .size UmicomRiscvUserTrapEntry, . - UmicomRiscvUserTrapEntry
    .size UmicomRiscvUserExecute, . - UmicomRiscvUserExecute

    /* Only this section and the user C payload get executable user mappings. */
    .section .text.umicom_user_payload, "ax", @progbits
    .balign 16
    .global UmicomRiscvUserEntry
    .type UmicomRiscvUserEntry, @function
UmicomRiscvUserEntry:
    call UmicomRiscvUserPayload
    li a7, UMICOM_USER_CALL_EXIT
    .global UmicomRiscvUserExitInstruction
UmicomRiscvUserExitInstruction:
    ecall /* User ECALL is architectural exception 8. */
    j UmicomRiscvUserExitInstruction /* EXIT must never be resumed by the monitor. */
    .size UmicomRiscvUserEntry, . - UmicomRiscvUserEntry

    .global UmicomRiscvUserSystemCall
    .type UmicomRiscvUserSystemCall, @function
UmicomRiscvUserSystemCall:
    /* Adapt normal C arguments to the explicit environment-call register ABI. */
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    ecall /* Dispatcher returns a0 and resumes at the following instruction. */
    ret
    .size UmicomRiscvUserSystemCall, . - UmicomRiscvUserSystemCall

    .global UmicomRiscvUserLoad
    .type UmicomRiscvUserLoad, @function
UmicomRiscvUserLoad:
    .global UmicomRiscvUserLoadInstruction
UmicomRiscvUserLoadInstruction:
    ld a0, 0(a0) /* Deliberately test either an unmapped or supervisor-only page. */
    ret
    .size UmicomRiscvUserLoad, . - UmicomRiscvUserLoad

    .global UmicomRiscvUserStore
    .type UmicomRiscvUserStore, @function
UmicomRiscvUserStore:
    .global UmicomRiscvUserStoreInstruction
UmicomRiscvUserStoreInstruction:
    sd a1, 0(a0) /* A read-only mapping must fault before changing its frame. */
    ret
    .size UmicomRiscvUserStore, . - UmicomRiscvUserStore

    .global UmicomRiscvUserSupervisorCsrRead
    .type UmicomRiscvUserSupervisorCsrRead, @function
UmicomRiscvUserSupervisorCsrRead:
    .global UmicomRiscvUserSupervisorCsrInstruction
UmicomRiscvUserSupervisorCsrInstruction:
    csrr a0, satp /* U-mode cannot read even a supervisor control register. */
    ret
    .size UmicomRiscvUserSupervisorCsrRead, . - UmicomRiscvUserSupervisorCsrRead

    .global UmicomRiscvUserJump
    .type UmicomRiscvUserJump, @function
UmicomRiscvUserJump:
    jalr zero, 0(a0) /* Target is a data page; instruction fetch must be refused. */
    .size UmicomRiscvUserJump, . - UmicomRiscvUserJump

    .global UmicomRiscvUserExitWithInvalidStack
    .type UmicomRiscvUserExitWithInvalidStack, @function
UmicomRiscvUserExitWithInvalidStack:
    mv sp, a0 /* Deliberately destroy the user stack before asking to exit. */
    li a0, 0
    li a7, UMICOM_USER_CALL_EXIT
    .global UmicomRiscvUserInvalidStackExitInstruction
UmicomRiscvUserInvalidStackExitInstruction:
    ecall /* The first machine trap instruction must not store through this sp. */
    j UmicomRiscvUserInvalidStackExitInstruction
    .size UmicomRiscvUserExitWithInvalidStack, . - UmicomRiscvUserExitWithInvalidStack

    .global UmicomRiscvUserBusyLoop
    .type UmicomRiscvUserBusyLoop, @function
UmicomRiscvUserBusyLoop:
    .global UmicomRiscvUserBusyInstruction
UmicomRiscvUserBusyInstruction:
    j UmicomRiscvUserBusyInstruction /* Only the machine timer can end this run. */
    .size UmicomRiscvUserBusyLoop, . - UmicomRiscvUserBusyLoop

    .global UmicomRiscvUserRegisterProbe
    .type UmicomRiscvUserRegisterProbe, @function
UmicomRiscvUserRegisterProbe:
    /* Preserve callee-saved values before deliberately seeding several register
     * classes. A resumed system call must not destroy the interrupted values. */
    addi sp, sp, -32
    sd s0, 0(sp)
    sd s1, 8(sp)
    sd ra, 16(sp)
    li t0, 32 /* Seed t0 for comparison after ECALL. */
    li t1, 33 /* Seed t1 for comparison after ECALL. */
    li t2, 34 /* Seed t2 for comparison after ECALL. */
    li t3, 35 /* Seed t3 for comparison after ECALL. */
    li t4, 36 /* Seed t4 for comparison after ECALL. */
    li t5, 37 /* Seed t5 for comparison after ECALL. */
    li t6, 38 /* Seed t6 for comparison after ECALL. */
    li a2, 39 /* Seed a2 for comparison after ECALL. */
    li a3, 40 /* Seed a3 for comparison after ECALL. */
    li a4, 41 /* Seed a4 for comparison after ECALL. */
    li a5, 42 /* Seed a5 for comparison after ECALL. */
    li a6, 43 /* Seed a6 for comparison after ECALL. */
    li s0, 44 /* Seed s0 for comparison after ECALL. */
    li s1, 45 /* Seed s1 for comparison after ECALL. */
    li a7, UMICOM_USER_CALL_IDENTITY
    ecall
    li a1, 32
    bne t0, a1, .Lumicom_user_register_mismatch
    li a1, 33
    bne t1, a1, .Lumicom_user_register_mismatch
    li a1, 34
    bne t2, a1, .Lumicom_user_register_mismatch
    li a1, 35
    bne t3, a1, .Lumicom_user_register_mismatch
    li a1, 36
    bne t4, a1, .Lumicom_user_register_mismatch
    li a1, 37
    bne t5, a1, .Lumicom_user_register_mismatch
    li a1, 38
    bne t6, a1, .Lumicom_user_register_mismatch
    li a1, 39
    bne a2, a1, .Lumicom_user_register_mismatch
    li a1, 40
    bne a3, a1, .Lumicom_user_register_mismatch
    li a1, 41
    bne a4, a1, .Lumicom_user_register_mismatch
    li a1, 42
    bne a5, a1, .Lumicom_user_register_mismatch
    li a1, 43
    bne a6, a1, .Lumicom_user_register_mismatch
    li a1, 44
    bne s0, a1, .Lumicom_user_register_mismatch
    li a1, 45
    bne s1, a1, .Lumicom_user_register_mismatch
    li a0, 1 /* All sampled temporary, argument and saved registers survived. */
    j .Lumicom_user_register_return
.Lumicom_user_register_mismatch:
    li a0, 0
.Lumicom_user_register_return:
    ld s0, 0(sp)
    ld s1, 8(sp)
    ld ra, 16(sp)
    addi sp, sp, 32
    ret
    .size UmicomRiscvUserRegisterProbe, . - UmicomRiscvUserRegisterProbe
