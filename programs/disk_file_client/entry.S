/* Umicom Kernel independently linked persistent-file client. The ELF loader
 * supplies its private user stack and zero-filled writable storage. There is
 * no Kernel pointer or direct filesystem call in this executable.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomKernelDiskFileProgramStart
    .type UmicomKernelDiskFileProgramStart, @function
UmicomKernelDiskFileProgramStart:
    call UmicomKernelDiskFileProgramMain
    li a7, 0 /* EXIT consumes the diagnostic result, not a Kernel address. */
    ecall
    unimp
    .size UmicomKernelDiskFileProgramStart, . - UmicomKernelDiskFileProgramStart

    .section .text, "ax", @progbits
    .global UmicomKernelDiskFileProgramCall
    .type UmicomKernelDiskFileProgramCall, @function
UmicomKernelDiskFileProgramCall:
    li a7, 32 /* FILE; a0/a1 already contain request/result user addresses. */
    li a2, 48
    li a3, 16
    ecall
    ret /* A retained user context resumes here after dispatcher completion. */
    .size UmicomKernelDiskFileProgramCall, . - UmicomKernelDiskFileProgramCall
