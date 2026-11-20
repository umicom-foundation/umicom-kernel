/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/riscv_boot_handoff.c
 *
 * PURPOSE:
 *   Form a canonical boot observation from the RISC-V memory reservation plan.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/riscv64/boot_handoff.h"
#include "boot_memory.h"

UmicomBootHandoffStatus UmicomRiscvBootHandoffProject(const UmicomRiscvBootHandoffInput *input,
    const UmicomKernelBootReservationPlan *plan, UmicomKernelBootHandoff *out)
{
    if (!input || !plan || !out) return UMICOM_BOOT_HANDOFF_ARGUMENT;
    if (plan->count > UMICOM_KERNEL_BOOT_RESERVATION_LIMIT || plan->count > UMICOM_BOOT_HANDOFF_RESERVED_LIMIT)
        return UMICOM_BOOT_HANDOFF_LIMIT;

    /* Reuse the allocator's preflight plan. Keep the true image span distinct
     * from the pages the plan also protects before the linked image starts. */
    UmicomKernelBootHandoff record;
    UmicomBootMemoryClear(&record, sizeof record);
    record.architecture = UMICOM_BOOT_RISCV64;
    record.bootstrapCpu = input->hart;
    record.ramCount = 1U;
    record.ram[0].address = input->ram.address;
    record.ram[0].bytes = input->ram.bytes;
    record.kernel.address = input->kernel.address;
    record.kernel.bytes = input->kernel.bytes;
    record.tableKind = UMICOM_BOOT_TABLE_FDT;
    record.table.address = input->deviceTree.address;
    record.table.bytes = input->deviceTree.bytes;
    record.reservedCount = plan->count;
    for (UmicomSize i = 0U; i < plan->count; ++i) {
        record.reserved[i].address = plan->ranges[i].base;
        record.reserved[i].bytes = plan->ranges[i].bytes;
    }

    /* Keep the firmware-neutral reader as the final authority. Publish the
     * owned value from the versioned bytes instead of copying native padding. */
    UmicomU8 encoded[UMICOM_BOOT_HANDOFF_MAX_BYTES];
    UmicomSize length = 0U;
    UmicomBootHandoffStatus status = UmicomKernelBootHandoffWrite(&record, encoded, sizeof encoded, &length);
    if (status != UMICOM_BOOT_HANDOFF_OK) return status;
    return UmicomKernelBootHandoffRead(encoded, length, out);
}
