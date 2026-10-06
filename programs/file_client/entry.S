/* Umicom Kernel separately linked file-client entry. No hosted runtime is
 * assumed; the existing ELF loader supplies zeroed storage and the stack.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.entry, "ax", @progbits
    .balign 4
    .global UmicomFileProgramStart
    .type UmicomFileProgramStart, @function
UmicomFileProgramStart:
    call UmicomFileProgramMain /* a0 selects this diagnostic instance's role. */
    li a7, 0                 /* The original EXIT consumes the C return value. */
    ecall
    unimp                    /* A terminal invocation must not return here. */
    .size UmicomFileProgramStart, . - UmicomFileProgramStart
    .section .text, "ax", @progbits
    .global UmicomFileProgramCall
    .type UmicomFileProgramCall, @function
UmicomFileProgramCall:
    li a7, 32 /* FILE request; a0/a1 already carry the C request/result pointers. */
    li a2, 48 /* Explicit input wire size. */
    li a3, 16 /* Explicit result wire size. */
    ecall     /* The retained context resumes at ret after dispatcher completion. */
    ret
    .size UmicomFileProgramCall, . - UmicomFileProgramCall
    .global UmicomFileProgramIdentity
    .type UmicomFileProgramIdentity, @function
UmicomFileProgramIdentity:
    li a7, 1 /* Existing IDENTITY ignores user-supplied identity arguments. */
    ecall
    ret
    .size UmicomFileProgramIdentity, . - UmicomFileProgramIdentity
    .global UmicomFileProgramFault
    .type UmicomFileProgramFault, @function
UmicomFileProgramFault:
    ld a0, 0(zero) /* Deliberate unmapped address, not undefined C null dereference. */
    ret
    .size UmicomFileProgramFault, . - UmicomFileProgramFault
    .global UmicomFileProgramSpin
    .type UmicomFileProgramSpin, @function
UmicomFileProgramSpin:
    j UmicomFileProgramSpin /* Timer scheduling must regain control without a yield. */
    .size UmicomFileProgramSpin, . - UmicomFileProgramSpin
