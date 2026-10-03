/* Umicom Kernel native diagnostic executable layout.
 *
 * ELF program headers, rather than section names, drive the Kernel loader.
 * Give text, read-only constants and writable data separate pages so none of
 * their rights must be merged. The BSS contributes to memory size but not file
 * size, making zero-fill an observable part of the loading contract.
 *
 * Sammy Hegab, Umicom Foundation. MIT licence. */
OUTPUT_ARCH(riscv)
ENTRY(UmicomProgramEntry)
PHDRS
{
    text PT_LOAD FLAGS(5);   /* Read + execute, never write. */
    rodata PT_LOAD FLAGS(4); /* Read only. */
    data PT_LOAD FLAGS(6);   /* Read + write, never execute. */
}
SECTIONS
{
    . = 0x00400000; /* A user virtual address, not a chosen physical frame. */
    .text : { KEEP(*(.text.entry)) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } :rodata
    . = ALIGN(4096);
    .data : { *(.data .data.* .sdata .sdata.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) } :data
    /* Debug metadata stays outside PT_LOAD. It may help inspection but is not
     * granted a user mapping by this executable's process image. */
    .riscv.attributes 0 : { *(.riscv.attributes) }
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
