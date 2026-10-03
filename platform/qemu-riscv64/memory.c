/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/memory.c
 *
 * PURPOSE:
 *   Describe the physical RAM profile used by the K3 QEMU RISC-V `virt` test.
 *
 * EDUCATIONAL OVERVIEW:
 *   still runs one deliberately fixed machine profile:
 *
 *     QEMU machine:  virt
 *     command:       -m 128M
 *     RAM base:      0x80000000
 *     RAM bytes:     128 MiB
 *
 *   Later hardware-discovery work will derive memory regions from validated
 *   firmware/device-tree information.  K3 keeps this platform fact inside the
 *   QEMU adapter instead of leaking it into the generic allocator.
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

/* test command configures exactly 128 MiB of guest RAM. */
#define UMICOM_QEMU_RISCV64_RAM_BYTES \
    ((UmicomSize)128U * (UmicomSize)1024U * (UmicomSize)1024U)

void UmicomPlatformPhysicalMemoryDescribe(
    UmicomPlatformPhysicalMemoryInfo *outInfo
)
{
    /* A null output pointer has nowhere to receive the platform description. */
    if (outInfo == (UmicomPlatformPhysicalMemoryInfo *)0) {
        /* Leave machine state untouched and return safely. */
        return;
    }

    /* Publish QEMU `virt`'s documented first DRAM byte. */
    outInfo->base = UMICOM_QEMU_RISCV64_RAM_BASE;

    /* Publish the exact RAM byte count matched by the K3 `-m 128M` command. */
    outInfo->bytes = UMICOM_QEMU_RISCV64_RAM_BYTES;
}
