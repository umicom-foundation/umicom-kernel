/* Umicom Kernel separately linked blocking IPC entry.
 * No C runtime is present: the loader zeroes data/BSS and supplies an aligned
 * stack and startup argument. Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomIpcProgramStart
    .type UmicomIpcProgramStart, @function
UmicomIpcProgramStart:
    call UmicomIpcProgramMain /* a0 is the Kernel-admitted endpoint token. */
    li a7, 0                /* Existing native EXIT consumes the returned a0. */
    ecall
    unimp                   /* A successful EXIT cannot return to this program. */
    .size UmicomIpcProgramStart, . - UmicomIpcProgramStart

    .section .text, "ax", @progbits
    .global UmicomIpcProgramCall
    .type UmicomIpcProgramCall, @function
UmicomIpcProgramCall:
    mv a7, a0 /* Move the C operation number into the existing ECALL register. */
    mv a0, a1 /* Endpoint token. */
    mv a1, a2 /* User buffer address. */
    mv a2, a3 /* Payload size or receive capacity. */
    mv a3, a4 /* Relative deadline or explicit FOREVER. */
    ecall     /* A blocked invocation resumes at the following ret, not _start. */
    ret
    .size UmicomIpcProgramCall, . - UmicomIpcProgramCall
