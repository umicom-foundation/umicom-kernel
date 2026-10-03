/*-----------------------------------------------------------------------------
 * Umicom Kernel native diagnostic program
 * File: programs/diagnostic/entry.S
 *
 * PURPOSE:
 *   Enter a separately linked freestanding user program and expose its small
 *   native environment-call convention without linking any Kernel functions.
 *
 * EDUCATIONAL NOTE:
 *   The process loader, not this entry routine, must initialise .data and .bss.
 *   The Kernel supplies a mapped stack and one integer argument in a0. There
 *   is no libc startup, argc/argv, TLS setup or dynamic loader in this profile.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomProgramEntry
    .type UmicomProgramEntry, @function
UmicomProgramEntry:
    /* Preserve the Kernel argument in a0 as the C function's first argument. */
    call UmicomProgramMain
    /* Return the C result through the native EXIT service; there is no caller
     * in this address space to which a C return could safely transfer control. */
    li a7, UMICOM_USER_CALL_EXIT
    ecall
.Lumicom_program_no_return:
    /* EXIT should not return. A loop makes a broken monitor hit its deadline. */
    j .Lumicom_program_no_return
    .size UmicomProgramEntry, . - UmicomProgramEntry

    .section .text.calls, "ax", @progbits
    .global UmicomProgramSystemCall
    .type UmicomProgramSystemCall, @function
UmicomProgramSystemCall:
    /* C arguments are number, destination, source and count in a0-a3.
     * The native trap ABI expects number in a7 and arguments in a0-a2. */
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    ecall
    /* The established monitor changes a0 only, preserving the return address. */
    ret
    .size UmicomProgramSystemCall, . - UmicomProgramSystemCall

    .global UmicomProgramWriteReadOnly
    .type UmicomProgramWriteReadOnly, @function
UmicomProgramWriteReadOnly:
    /* This negative case must fault on the read-only segment supplied in a0. */
    sb zero, 0(a0)
    ret
    .size UmicomProgramWriteReadOnly, . - UmicomProgramWriteReadOnly

    .global UmicomProgramBusyLoop
    .type UmicomProgramBusyLoop, @function
UmicomProgramBusyLoop:
    /* No environment call can rescue this loop; the Kernel's timer must do it. */
    j UmicomProgramBusyLoop
    .size UmicomProgramBusyLoop, . - UmicomProgramBusyLoop
