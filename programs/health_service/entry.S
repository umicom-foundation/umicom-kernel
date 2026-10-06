/* Umicom native managed-service entry. Structured launch registers already hold
 * argc/argv/envp/info. Neither the callback nor its ECALL borrows a Kernel stack.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/service_report_abi.h"
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomHealthProgramStart
    .type UmicomHealthProgramStart, @function
UmicomHealthProgramStart:
    call UmicomHealthProgramMain
    li a7, 0                 /* A returned C result uses the unchanged EXIT call. */
    ecall
    unimp                    /* EXIT never returns to this instruction. */
    .size UmicomHealthProgramStart, . - UmicomHealthProgramStart

    .section .text, "ax", @progbits
    .global UmicomHealthProgramReport
    .type UmicomHealthProgramReport, @function
UmicomHealthProgramReport:
    li a2, 0                 /* Reserved report registers cannot request authority. */
    li a3, 0
    li a7, UMICOM_USER_CALL_SERVICE_REPORT
    ecall                    /* Successful reports resume here after a Kernel-owned delay. */
    ret
    .size UmicomHealthProgramReport, . - UmicomHealthProgramReport
