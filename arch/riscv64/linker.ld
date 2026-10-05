/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/linker.ld
 *
 * PURPOSE:
 *   Describe exactly where the first freestanding kernel lives in the QEMU
 *   RISC-V virtual machine's RAM and how its ELF sections are arranged.
 *
 * EDUCATIONAL NOTE:
 *   A hosted application normally lets the operating system's loader choose
 *   addresses.  A kernel is itself part of the loading/runtime environment, so
 *   this first milestone explicitly controls its memory layout.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Tell the ELF linker that output machine instructions use the RISC-V
 * architecture rather than the Windows/Linux host architecture. */
OUTPUT_ARCH(riscv)

/* Tell ELF/QEMU that execution begins at the Assembly symbol named _start. */
ENTRY(_start)

/* Define the memory region the first-boot foundation is allowed to occupy.
 *
 * QEMU's RISC-V virt RAM starts at 0x80000000.  the first-boot foundation deliberately uses
 * 0x80200000 as its linked start so the simple direct-kernel experiment has a
 * clear fixed location with room below it reserved for future boot concerns.
 *
 * QEMU's "-m 128M" RAM spans 0x80000000 through 0x87ffffff.
 *
 * the first-boot foundation deliberately starts at 0x80200000, leaving the first 2 MiB below the
 * kernel image outside this linker's ownership.  Therefore only 126 MiB remain
 * between our link origin and the end of the emulated RAM.  Describing 128 MiB
 * here would incorrectly claim ownership of two MiB beyond the real machine's
 * configured RAM even though the tiny the first-boot foundation image would not immediately reach it.
 */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 126M
}

/* Lay out each program section inside the RAM region. */
SECTIONS
{
    /* Move the linker's current-address cursor to the beginning of RAM. */
    . = ORIGIN(RAM);

    /* Publish the first byte of the complete kernel image as a linker symbol. */
    __kernel_start = .;

    /* Place executable instructions on a 4 KiB boundary. */
    .text : ALIGN(4096)
    {
        /* KEEP prevents garbage collection from discarding the boot entry even
         * if no normal C function has an explicit reference to _start. */
        KEEP(*(.text.boot))

        /* Give the supervisor payload complete executable pages of its own.
         * Its page table maps this range RX, not the surrounding machine code.
         * The boot entry above remains at the established image entry address. */
        . = ALIGN(4096);
        __umicom_supervisor_text_start = .;
        KEEP(*(.text.umicom_supervisor_payload))
        . = ALIGN(4096);
        __umicom_supervisor_text_end = .;

        /* User executable pages are separate from both supervisor payload and
         * machine monitor code. Each user root maps only this range RX + USER;
         * no surrounding machine function becomes callable by sharing a page. */
        . = ALIGN(4096);
        __umicom_user_text_start = .;
        KEEP(*(.text.umicom_user_payload))
        . = ALIGN(4096);
        __umicom_user_text_end = .;

        /* Keep the guarded-stack probes on private executable pages. Their
         * copies are mapped RX; no surrounding machine code is made callable. */
        . = ALIGN(4096);
        __umicom_region_text_start = .;
        KEEP(*(.text.umicom_region_payload))
        . = ALIGN(4096);
        __umicom_region_text_end = .;

        /* Collect all other executable text sections produced by C/Assembly. */
        *(.text .text.*)
    } > RAM

    /* Place read-only constants such as string literals on the next page. */
    .rodata : ALIGN(4096)
    {
        /* Collect standard and compiler-created read-only data sections. */
        *(.rodata .rodata.*)
    } > RAM

    /* Place explicitly initialized writable global/static objects next. */
    .data : ALIGN(4096)
    {
        /* Collect standard and compiler-created initialized data sections. */
        *(.data .data.*)
    } > RAM

    /* Reserve zero-initialized objects without storing their zero bytes in the
     * ELF file.  boot.S clears this range before C code starts. */
    .bss (NOLOAD) : ALIGN(4096)
    {
        /* Export the first byte that early Assembly must clear. */
        __bss_start = .;

        /* Collect normal zero-initialized objects. */
        *(.bss .bss.*)

        /* Collect legacy/common zero-initialized symbols if a tool emits any. */
        *(COMMON)

        /* Round the end upward to an eight-byte boundary because boot.S clears
         * BSS with 64-bit stores. */
        . = ALIGN(8);

        /* Export the address immediately after BSS. */
        __bss_end = .;
    } > RAM

    /* Reserve a fixed early stack.
     *
     * the first-boot foundation has no dynamic allocator, virtual memory or per-thread stacks yet.
     * A single 64 KiB bootstrap stack is intentionally simple and visible.
     */
    .bootstrap_stack (NOLOAD) : ALIGN(16)
    {
        /* Mark the low address of the reserved stack range. */
        __stack_bottom = .;

        /* Advance the address cursor by 64 KiB, reserving the memory. */
        . += 64K;

        /* The RISC-V stack grows toward lower addresses, so execution starts
         * with sp pointing at this high address. */
        __stack_top = .;
    } > RAM

    /* Round the image end to a page boundary for later memory-management work. */
    . = ALIGN(4096);

    /* Export the first byte after all the first-boot foundation kernel-owned memory. */
    __kernel_end = .;

    /* Discard hosted/compiler metadata that the first-boot foundation cannot consume and does not need
     * in its tiny freestanding image. */
    /DISCARD/ :
    {
        /* Compiler identification comments are not runtime kernel state. */
        *(.comment)

        /* Remove ELF note sections, including optional build-note metadata. */
        *(.note*)

        /* the first-boot foundation has no C++/exception stack unwinder. */
        *(.eh_frame*)
    }
}

/* Fail the link rather than silently allowing the image to exceed the RAM
 * region promised by this milestone's QEMU configuration. */
ASSERT(
    __kernel_end <= ORIGIN(RAM) + LENGTH(RAM),
    "Umicom Kernel the first-boot foundation image exceeds the configured QEMU RAM region"
)

/* Bound instruction inspection to executable bytes, not all Kernel memory.
 * The trap guard validates this range before reading an ECALL encoding. */
__umicom_trap_text_begin = ADDR(.text);
__umicom_trap_text_end = ADDR(.text) + SIZEOF(.text);
