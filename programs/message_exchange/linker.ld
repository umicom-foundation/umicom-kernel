/* Umicom Kernel native message-exchange executable.
 * Separate PT_LOAD permission classes let the existing ELF loader keep code
 * read/execute and data read/write. The program has no Kernel physical address.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
OUTPUT_ARCH(riscv)
ENTRY(UmicomMessageProgramEntry)
PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x00400000;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } :rodata
    . = ALIGN(4096);
    .data : { *(.data .data.* .sdata .sdata.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) } :data
    .riscv.attributes 0 : { *(.riscv.attributes) }
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
