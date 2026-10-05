/* Umicom Kernel separately loaded scheduling diagnostic entry and calls.
 * The existing native register ABI is unchanged. No hosted startup is linked.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/user_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomScheduledEntry
    .type UmicomScheduledEntry, @function
UmicomScheduledEntry:
    call UmicomScheduledMain /* Its nested C stack remains alive across pre-emption. */
    li a7, UMICOM_USER_CALL_EXIT
    ecall
    unimp /* A successful EXIT must never resume this instruction. */
    .size UmicomScheduledEntry, . - UmicomScheduledEntry

    .section .text.umicom_scheduled_calls, "ax", @progbits
    .global UmicomScheduledSystemCall
    .type UmicomScheduledSystemCall, @function
UmicomScheduledSystemCall:
    mv a7, a0 /* Normal C arguments become the unchanged native call registers. */
    mv a0, a1
    mv a1, a2
    mv a2, a3
    ecall
    ret
    .size UmicomScheduledSystemCall, . - UmicomScheduledSystemCall

    .global UmicomScheduledExitBadStack
    .type UmicomScheduledExitBadStack, @function
UmicomScheduledExitBadStack:
    li sp, 0 /* Returning to the Kernel must never store through this user value. */
    li a0, 0x605
    li a7, UMICOM_USER_CALL_EXIT
    ecall
    unimp
    .size UmicomScheduledExitBadStack, . - UmicomScheduledExitBadStack
