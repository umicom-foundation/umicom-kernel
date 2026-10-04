/*-----------------------------------------------------------------------------
 * Umicom Kernel native message-exchange program
 * File: programs/message_exchange/entry.S
 *
 * PURPOSE:
 *   Enter a separately loaded program with its endpoint handle in a0 and pass
 *   its result back through the existing EXIT call. No Kernel symbol is linked.
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
    .global UmicomMessageProgramEntry
    .type UmicomMessageProgramEntry, @function
UmicomMessageProgramEntry:
    /* The loader already created the stack and initialised data/BSS. */
    call UmicomMessageProgramMain
    /* The C result becomes the process exit value; there is no external caller. */
    li a7, UMICOM_USER_CALL_EXIT
    ecall
.Lumicom_message_exit_refused:
    /* A broken EXIT must meet the monitor deadline, not run beyond text. */
    j .Lumicom_message_exit_refused
    .size UmicomMessageProgramEntry, . - UmicomMessageProgramEntry

    .section .text.calls, "ax", @progbits
    .global UmicomMessageProgramCall
    .type UmicomMessageProgramCall, @function
UmicomMessageProgramCall:
    /* C supplies number/first/second/third in a0-a3. Move only the ABI arguments. */
    mv a7, a0
    mv a0, a1
    mv a1, a2
    mv a2, a3
    ecall
    /* The monitor preserves the saved return address and registers other than a0. */
    ret
    .size UmicomMessageProgramCall, . - UmicomMessageProgramCall
