/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/boot.S
 *
 * PURPOSE:
 *   Establish the minimum RV64 machine state required to enter freestanding
 *   C23 code: select the boot hart, disable machine interrupts, establish the
 *   bootstrap stack, clear BSS and preserve the QEMU-provided device-tree
 *   pointer for KernelMain.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

.section .text.boot, "ax", @progbits
.align 4
.global _start
.type _start, @function

_start:
    /* K1 runs one boot hart. Secondary harts are parked so increasing QEMU's
     * CPU count later cannot make multiple harts race through early init. */
    csrr s1, mhartid
    bnez s1, .Lpark

    /* Keep interrupts disabled until a later milestone installs trap state. */
    csrw mie, zero

    /* QEMU's RISC-V boot protocol supplies the device-tree address in a1. */
    mv s2, a1

    /* The linker reserves a dedicated bootstrap stack after BSS. */
    la sp, __stack_top

    /* C requires zero-initialised BSS. The linker keeps both boundaries
     * eight-byte aligned, so RV64 stores can clear it efficiently. */
    la t0, __bss_start
    la t1, __bss_end
.Lclear_bss:
    bgeu t0, t1, .Lbss_ready
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_bss

.Lbss_ready:
    mv a0, s1
    mv a1, s2
    call UmiKernelMain

    /* KernelMain is not expected to return. Park safely if it does. */
.Lpark:
    wfi
    j .Lpark

.size _start, . - _start
