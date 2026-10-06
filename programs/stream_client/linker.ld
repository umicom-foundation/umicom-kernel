/* Umicom Kernel independent stream-client executable layout. Three non-overlapping
 * permission classes avoid writable code. Sammy Hegab, Umicom Foundation. MIT. */
OUTPUT_ARCH(riscv)
ENTRY(UmicomStreamProgramStart)
PHDRS {
    text PT_LOAD FLAGS(5);   /* Read and execute, never write. */
    rodata PT_LOAD FLAGS(4); /* Immutable name/constant data. */
    data PT_LOAD FLAGS(6);   /* Writable observations and buffers, never execute. */
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
ASSERT(SIZEOF(.text) < 0x00100000, "Stream-client executable text must not overlap fixed data")
