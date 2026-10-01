/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/linker.ld
 *
 * PURPOSE:
 *   Place the first freestanding RISC-V kernel at 0x80200000, the conventional
 *   QEMU virt direct-kernel load region above the machine's DRAM base. K1 uses
 *   a single 64 KiB bootstrap stack and no dynamic allocation.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);
    __kernel_start = .;

    .text : ALIGN(4096)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > RAM

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    } > RAM

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    } > RAM

    .bss (NOLOAD) : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM

    .bootstrap_stack (NOLOAD) : ALIGN(16)
    {
        __stack_bottom = .;
        . += 64K;
        __stack_top = .;
    } > RAM

    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

ASSERT(__kernel_end <= ORIGIN(RAM) + LENGTH(RAM),
       "Umicom Kernel K1 image exceeds the configured QEMU RAM region")
