/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/boot.S
 *
 * PURPOSE:
 *   Perform the minimum machine-level work needed before portable C23 kernel
 *   code can run safely.
 *
 * EDUCATIONAL OVERVIEW:
 *   When QEMU transfers control to this image, we do not have an operating
 *   system, libc, a C runtime startup file or a prepared C stack.  This file
 *   therefore creates that tiny initial environment by hand.
 *
 * RISC-V REGISTER NOTES USED HERE:
 *   a0-a7  = function argument/return registers.
 *   s1/s2  = "saved" registers; we use them to preserve boot values.
 *   t0/t1  = temporary registers used while clearing BSS.
 *   sp     = stack pointer.
 *   zero   = a register whose value is always integer zero.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

    /* Put the following instructions in a dedicated executable code section.
     * The linker script uses this section to guarantee that _start appears
     * before ordinary C functions. */
    .section .text.boot, "ax", @progbits

    /* Align the entry code on a 2^4 = 16-byte boundary. */
    .align 4

    /* Export _start so the ELF linker can use it as the image entry symbol. */
    .global _start

    /* Mark _start as a function symbol in ELF debugging/symbol metadata. */
    .type _start, @function

_start:
    /* Read the hardware-thread ("hart") identifier from the mhartid CSR.
     *
     * QEMU can emulate more than one RISC-V hart.  K1 deliberately boots only
     * hart 0 so two processors cannot race through global early initialisation.
     * Store the ID in s1 so it survives the remaining bootstrap work. */
    csrr s1, mhartid

    /* If the hart ID is non-zero, branch to the permanent parking loop. */
    bnez s1, .Lpark

    /* Clear the machine interrupt-enable CSR.
     *
     * K1 has not installed a trap vector, so accepting interrupts now would
     * transfer control to an undefined handler address. */
    csrw mie, zero

    /* QEMU's RISC-V direct boot provides a device-tree blob address in a1.
     *
     * The future kernel will parse this structure instead of hard-coding every
     * device address.  K1 only reports the pointer, so preserve it in s2 while
     * we reuse argument registers during startup. */
    mv s2, a1

    /* Load the linker-defined top of the bootstrap stack into sp.
     *
     * C functions need a valid stack before they can create automatic local
     * variables or save return addresses/registers. */
    la sp, __stack_top

    /* Load the first byte of the BSS region into temporary register t0.
     *
     * C requires objects with static storage and no explicit initializer to
     * begin as zero. */
    la t0, __bss_start

    /* Load the first address after BSS into temporary register t1. */
    la t1, __bss_end

.Lclear_bss:
    /* Once t0 has reached or passed t1, every BSS byte has been cleared and
     * execution can continue to C startup. */
    bgeu t0, t1, .Lbss_ready

    /* Store one 64-bit zero at the current BSS address.
     *
     * The linker guarantees that the end is eight-byte aligned, allowing
     * efficient RV64 double-word stores. */
    sd zero, 0(t0)

    /* Advance to the next eight bytes of BSS. */
    addi t0, t0, 8

    /* Repeat until the BSS end marker is reached. */
    j .Lclear_bss

.Lbss_ready:
    /* The RISC-V C calling convention passes the first argument in a0.
     * Give UmiKernelMain() the boot hart ID. */
    mv a0, s1

    /* The second C argument uses a1.
     * Restore the preserved device-tree address into that argument register. */
    mv a1, s2

    /* Call the portable C23 kernel entry.
     *
     * "call" records a return address even though a successful kernel should
     * never return to this bootstrap code. */
    call UmiKernelMain

.Lpark:
    /* WFI means "Wait For Interrupt".
     *
     * Interrupts are disabled in K1, so this acts as a low-activity halt point
     * for secondary harts or the impossible case where KernelMain returns. */
    wfi

    /* If the processor resumes from WFI for any reason, immediately park
     * again rather than executing unrelated memory. */
    j .Lpark

    /* Record the function size in ELF symbol metadata for debuggers/tools. */
    .size _start, . - _start
