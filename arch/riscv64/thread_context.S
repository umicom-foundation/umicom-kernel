/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/thread_context.S
 *
 * PURPOSE:
 *   Switch cooperative Kernel call stacks without changing the trap vector,
 *   page-table root, interrupt policy or established user execution monitor.
 *
 * EDUCATIONAL OVERVIEW:
 *   The lp64 calling convention requires s0..s11 and sp to survive a call.
 *   Saving ra supplies the eventual continuation; gp and tp remain explicit
 *   even though the current build disables small-data and has no TLS service.
 *   Caller-saved registers need no extra save at this ordinary function-call
 *   boundary. This would not be enough for an asynchronous interrupt switch.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/thread_context.h"
    .section .text.umicom_thread_context, "ax", @progbits
    .balign 16

    .global UmicomKernelThreadMachineReady
    .type UmicomKernelThreadMachineReady, @function
UmicomKernelThreadMachineReady:
    /* These CSR reads require our documented machine-mode caller. */
    csrr t0, mhartid
    bnez t0, .Lumicom_thread_not_ready /* Only one hart owns this scheduler. */
    csrr t0, satp
    bnez t0, .Lumicom_thread_not_ready /* Do not switch translated Kernel stacks. */
    csrr t0, mie
    bnez t0, .Lumicom_thread_not_ready /* No live interrupt policy is borrowed. */
    csrr t0, mstatus
    li t1, 0x26608 /* MPRV, FS, VS and MIE must all be clear. */
    and t0, t0, t1
    bnez t0, .Lumicom_thread_not_ready /* No unsaved floating/vector state. */
    li a0, 1 /* Machine state meets the narrow cooperative contract. */
    ret
.Lumicom_thread_not_ready:
    li a0, 0 /* Refuse rather than pretend an unsafe context can be saved. */
    ret
    .size UmicomKernelThreadMachineReady, . - UmicomKernelThreadMachineReady

    .global UmicomKernelThreadContextPrepare
    .type UmicomKernelThreadContextPrepare, @function
UmicomKernelThreadContextPrepare:
    /* a0=context, a1=aligned stack top, a2=stable scheduler, a3=slot.
     * The scheduler has scrubbed the context. Only startup values are needed. */
    la t0, UmicomKernelThreadBootstrap
    sd t0, UMICOM_THREAD_CONTEXT_RETURN(a0) /* First dispatch begins bootstrap. */
    sd a1, UMICOM_THREAD_CONTEXT_STACK(a0) /* Each record has its own stack. */
    sd a2, 16(a0) /* Bootstrap receives the scheduler through saved s0. */
    sd a3, 24(a0) /* Bootstrap receives the slot through saved s1. */
    sd gp, UMICOM_THREAD_CONTEXT_GP(a0) /* Start with the caller's global base. */
    sd tp, UMICOM_THREAD_CONTEXT_TP(a0) /* Start with the caller's thread pointer. */
    ret
    .size UmicomKernelThreadContextPrepare, . - UmicomKernelThreadContextPrepare

    .global UmicomKernelThreadContextSwitch
    .type UmicomKernelThreadContextSwitch, @function
UmicomKernelThreadContextSwitch:
    /* a0 names the outgoing owner, a1 the incoming owner. Neither is on a user
     * stack. Save the complete cooperative context before loading any of next. */
    sd ra, 0(a0)   /* Continue just after this call when the thread is resumed. */
    sd sp, 8(a0)   /* Preserve this thread's live C activation records. */
    sd s0, 16(a0)  /* Preserve frame pointer or callee-saved s0. */
    sd s1, 24(a0)  /* Preserve callee-saved s1. */
    sd s2, 32(a0)  /* Preserve callee-saved s2. */
    sd s3, 40(a0)  /* Preserve callee-saved s3. */
    sd s4, 48(a0)  /* Preserve callee-saved s4. */
    sd s5, 56(a0)  /* Preserve callee-saved s5. */
    sd s6, 64(a0)  /* Preserve callee-saved s6. */
    sd s7, 72(a0)  /* Preserve callee-saved s7. */
    sd s8, 80(a0)  /* Preserve callee-saved s8. */
    sd s9, 88(a0)  /* Preserve callee-saved s9. */
    sd s10, 96(a0) /* Preserve callee-saved s10. */
    sd s11,104(a0) /* Preserve callee-saved s11. */
    sd gp,112(a0)  /* Keep the explicit global-pointer contract. */
    sd tp,120(a0)  /* Keep the explicit thread-pointer contract. */
    ld ra, 0(a1)   /* New continuation, not the outgoing return address. */
    ld sp, 8(a1)   /* No outgoing stack memory is touched after this point. */
    ld s0,16(a1)   /* Restore incoming s0. */
    ld s1,24(a1)   /* Restore incoming s1. */
    ld s2,32(a1)   /* Restore incoming s2. */
    ld s3,40(a1)   /* Restore incoming s3. */
    ld s4,48(a1)   /* Restore incoming s4. */
    ld s5,56(a1)   /* Restore incoming s5. */
    ld s6,64(a1)   /* Restore incoming s6. */
    ld s7,72(a1)   /* Restore incoming s7. */
    ld s8,80(a1)   /* Restore incoming s8. */
    ld s9,88(a1)   /* Restore incoming s9. */
    ld s10,96(a1)  /* Restore incoming s10. */
    ld s11,104(a1) /* Restore incoming s11. */
    ld gp,112(a1)  /* Restore incoming gp. */
    ld tp,120(a1)  /* Restore incoming tp. */
    ret            /* Resume a suspended call or start a fresh thread. */
    .size UmicomKernelThreadContextSwitch, . - UmicomKernelThreadContextSwitch

    .type UmicomKernelThreadBootstrap, @function
UmicomKernelThreadBootstrap:
    mv a0, s0 /* Fresh contexts carry their stable owner in s0. */
    mv a1, s1 /* The slot identifies its callback and borrowed argument. */
    call UmicomKernelThreadStart /* C records completion and switches away. */
    unimp /* Returning here violates the non-returning completion contract. */
    .size UmicomKernelThreadBootstrap, . - UmicomKernelThreadBootstrap
