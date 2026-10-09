/* Umicom Kernel independent persistent-file diagnostic layout. Executable,
 * immutable and writable pages remain separate permission classes.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
OUTPUT_ARCH(riscv)
ENTRY(UmicomKernelDiskFileProgramStart)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x00400000;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } :rodata
    . = 0x00600000;
    .data : { *(.data .data.* .sdata .sdata.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment .eh_frame .eh_frame_hdr) }
}
ASSERT(SIZEOF(.text) < 0x00100000, "Persistent-file diagnostic text must precede its fixed data")
