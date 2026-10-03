/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/executable_cache.S
 *
 * PURPOSE:
 *   Make newly populated executable frames visible to instruction fetch on
 *   the hart which will run them. Page-table fences alone do not do this.
 *
 * EDUCATIONAL NOTE:
 *   The loader writes instructions as data. FENCE.I orders those earlier stores
 *   against later instruction fetches on this same hart. It neither validates
 *   an executable nor synchronises another hart. The build enables Zifencei
 *   only for this source; the selected QEMU CPU implements that extension.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
    /* Keep the cache primitive in the Kernel, not in the user executable. */
    .section .text.umicom_executable_cache, "ax", @progbits
    .balign 4
    .global UmicomRiscvExecutableSynchronize
    .type UmicomRiscvExecutableSynchronize, @function
UmicomRiscvExecutableSynchronize:
    /* Earlier data writes to code frames must precede this hart's execution. */
    fence.i
    /* The caller still has its original machine stack and return address. */
    ret
    .size UmicomRiscvExecutableSynchronize, . - UmicomRiscvExecutableSynchronize
