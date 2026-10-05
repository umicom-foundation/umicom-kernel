/* Umicom Kernel scheduled diagnostic ELF. Separate pages preserve W^X.
 * The observation address is shared with the test harness, not a physical RAM
 * address. The loader supplies independent backing frames for every instance.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
OUTPUT_ARCH(riscv)
ENTRY(UmicomScheduledEntry)
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
    . = 0x00600000;
    .data : { KEEP(*(.data.umicom_scheduled_observation)) *(.data .data.* .sdata .sdata.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) } :data
    .riscv.attributes 0 : { *(.riscv.attributes) }
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
ASSERT(SIZEOF(.text) < 0x00100000, "Scheduled text must not overlap the observation region")
