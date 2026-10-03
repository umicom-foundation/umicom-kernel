/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/memory.c
 *
 * PURPOSE:
 *   Describe the physical RAM geometry selected by the QEMU RISC-V `virt`
 *   validation configuration.
 *
 * EDUCATIONAL OVERVIEW:
 *   The current QEMU command uses one deliberately simple memory profile:
 *
 *     machine:    virt
 *     option:     -m 128M
 *     RAM base:   0x80000000
 *     RAM bytes:  128 MiB
 *
 *   The physical allocator should not hard-code those machine facts.  Keeping
 *   them in this adapter allows later hardware discovery to replace this fixed
 *   profile with validated device-tree regions without redesigning allocator
 *   ownership rules.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import the platform memory-description contract. */
#include "umicom/kernel/platform.h"

/* QEMU RISC-V `virt` begins DRAM at physical address 0x80000000. */
#define UMICOM_QEMU_RISCV64_RAM_BASE ((UmicomAddress)0x80000000ULL)

/* The validation command configures exactly 128 MiB of guest RAM. */
#define UMICOM_QEMU_RISCV64_RAM_BYTES \
    ((UmicomSize)128U * (UmicomSize)1024U * (UmicomSize)1024U)

void UmicomPlatformPhysicalMemoryDescribe(
    UmicomPlatformPhysicalMemoryInfo *outInfo
)
{
    /* A null pointer cannot receive the platform geometry.  This low-level
     * query has no reason to mutate machine state on invalid input. */
    if (outInfo == (UmicomPlatformPhysicalMemoryInfo *)0) {
        /* Leave machine state untouched and return safely. */
        return;
    }

    /* Publish the first physical byte of QEMU's DRAM region. */
    outInfo->base = UMICOM_QEMU_RISCV64_RAM_BASE;

    /* Publish the exact byte count matched by the `-m 128M` test configuration. */
    outInfo->bytes = UMICOM_QEMU_RISCV64_RAM_BYTES;
}
