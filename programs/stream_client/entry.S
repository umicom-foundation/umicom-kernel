/* Umicom Kernel native stream client. Only this wrapper knows the register ABI.
 * Neither the program entry nor its stream calls borrow a Kernel C stack.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/stream_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomStreamProgramStart
    .type UmicomStreamProgramStart, @function
UmicomStreamProgramStart:
    call UmicomStreamProgramMain
    li a7, 0 /* EXIT uses the C result left in a0. */
    ecall
    unimp /* EXIT must not resume this invocation. */
    .size UmicomStreamProgramStart, . - UmicomStreamProgramStart

    .section .text, "ax", @progbits
    .global UmicomStreamProgramCall
    .type UmicomStreamProgramCall, @function
UmicomStreamProgramCall:
    /* a0=call, a1=selector, a2=buffer, a3=count, a4=flags, a5=count output.
     * Save the local output pointer and return address; the syscall changes a1.
     * It does not write through a5 or any other caller-supplied Kernel pointer. */
    addi sp, sp, -16
    sd ra, 0(sp)
    sd a5, 8(sp)
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    mv a3, a4
    ecall
    ld t0, 8(sp)
    sd a1, 0(t0) /* Store into the program's own stack after returning to U-mode. */
    ld ra, 0(sp)
    addi sp, sp, 16
    ret
    .size UmicomStreamProgramCall, . - UmicomStreamProgramCall

    .global UmicomStreamProgramFault
    .type UmicomStreamProgramFault, @function
UmicomStreamProgramFault:
    unimp /* Deliberate user fault for retained-output cleanup validation. */
    ret
    .size UmicomStreamProgramFault, . - UmicomStreamProgramFault
