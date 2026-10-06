/* Umicom native structured entry and stream wrapper.
 * The scheduler supplies four documented argument registers and an aligned sp.
 * CALL follows the ordinary RV64 C convention; no Kernel stack is borrowed.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/stream_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomLaunchProgramStart
    .type UmicomLaunchProgramStart, @function
UmicomLaunchProgramStart:
    call UmicomLaunchProgramMain /* argc, argv, envp and information remain in a0..a3. */
    li a7, 0 /* Return the C result through the established EXIT service. */
    ecall
    unimp /* EXIT must not resume this invocation. */
    .size UmicomLaunchProgramStart, . - UmicomLaunchProgramStart

    .section .text, "ax", @progbits
    .global UmicomLaunchProgramWrite
    .type UmicomLaunchProgramWrite, @function
UmicomLaunchProgramWrite:
    /* a0=buffer, a1=bytes, a2=transferred output. Preserve the latter on this
     * program's stack because STREAM_WRITE returns its byte count in a1. */
    addi sp, sp, -16
    sd ra, 0(sp)
    sd a2, 8(sp)
    mv a2, a1
    mv a1, a0
    li a0, UMICOM_STREAM_OUTPUT
    li a3, 0 /* Bounded copied writes may wait for output capacity. */
    li a7, UMICOM_USER_CALL_STREAM_WRITE
    ecall
    ld t0, 8(sp)
    sd a1, 0(t0)
    ld ra, 0(sp)
    addi sp, sp, 16
    ret
    .size UmicomLaunchProgramWrite, . - UmicomLaunchProgramWrite
