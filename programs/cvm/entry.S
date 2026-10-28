/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/entry.S
 *
 * PURPOSE:
 *   Connect the bounded CVM runtime to the native user-program contract.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomProgramEntry
    .type UmicomProgramEntry, @function
UmicomProgramEntry:
    /* The existing loader supplies the stack and an integer selector in a0. */
    call UmicomProgramMain
    li a7, UMICOM_USER_CALL_EXIT
    ecall
.Lcvm_exit_returned:
    j .Lcvm_exit_returned
    .size UmicomProgramEntry, . - UmicomProgramEntry
