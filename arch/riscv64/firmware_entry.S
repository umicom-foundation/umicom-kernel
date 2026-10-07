/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/firmware_entry.S
 *
 * PURPOSE:
 *   Bridge QEMU's machine-mode firmware hand-off at the beginning of RAM to
 *   the established Kernel entry at 0x80200000 without replacing that entry.
 *
 * QEMU's reset ROM prepares a0 (hart ID) and a1 (device-tree address) before
 * jumping to its firmware address. Loading an ELF as -bios does not make every
 * supported QEMU release jump to the ELF e_entry value. A separate executable
 * section at 0x80000000 therefore supplies the two instructions needed to reach
 * _start while retaining both boot arguments and the original Kernel layout.
 *
 * The first firmware page already belongs to the Kernel's reserved low-RAM
 * range. This trampoline creates no allocator permission and needs no stack,
 * data, device access, or hosted runtime. Only temporary register t0 changes.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/

    /* Keep two full-width instructions even when the rest of the Kernel uses
     * compressed instructions. Apply norelax before alignment as well: this
     * avoids an R_RISCV_ALIGN placeholder changing the apparent section size
     * while an older LLD evaluates linker assertions before relaxation. */
    .option push
    .option norvc
    .option norelax

    .section .text.umicom_firmware_entry, "ax", @progbits
    .balign 4
    .global UmicomKernelFirmwareEntry
    .type UmicomKernelFirmwareEntry, @function

UmicomKernelFirmwareEntry:
.LUmicomKernelFirmwareAddress:
    /* The relocation pair derives the destination from the existing _start
     * symbol. With the established page-aligned addresses, JALR has offset zero
     * and is the ordinary JR t0 instruction. No return address is requested. */
    auipc t0, %pcrel_hi(_start)
    jalr zero, %pcrel_lo(.LUmicomKernelFirmwareAddress)(t0)

    .size UmicomKernelFirmwareEntry, . - UmicomKernelFirmwareEntry
    .option pop
