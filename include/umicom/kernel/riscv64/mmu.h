/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/mmu.h
 *
 * PURPOSE:
 *   Publish the RISC-V machine-mode helpers used to validate that the Sv39
 *   page tables created by the Umicom Kernel are understood by the processor's
 *   real address-translation hardware.
 *
 * EDUCATIONAL OVERVIEW:
 *   The software page-table walker can prove that our data structures are
 *   internally consistent, but software inspection alone cannot prove that the
 *   RISC-V MMU interprets those entries the same way.
 *
 *   RISC-V offers a useful bridge while the Kernel still runs in machine mode:
 *
 *     - `satp` selects the supervisor address-translation root;
 *     - `MPRV` tells machine-mode load/store instructions to use the privilege
 *       level encoded in `mstatus.MPP`;
 *     - setting MPP to supervisor mode therefore lets one controlled load or
 *       store pass through the same Sv39 translation machinery that future
 *       supervisor-mode code will use.
 *
 *   Instruction fetch remains in machine mode during this validation.  That is
 *   intentional: we prove real data translation before asking the CPU to fetch
 *   Kernel instructions through translated addresses.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_RISCV64_MMU_H
#define UMICOM_KERNEL_RISCV64_MMU_H

#include "umicom/kernel/types.h"

/* RV64 satp bits 63:60 select the address-translation mode. */
#define UMICOM_RISCV_SATP_MODE_SHIFT ((UmicomU32)60U)

/* satp.MODE value 8 selects the three-level Sv39 translation scheme. */
#define UMICOM_RISCV_SATP_MODE_SV39 ((UmicomU64)8U)

/* Allow supervisor-effective validation accesses to reach physical RAM.
 *
 * QEMU's RISC-V CPU implements Physical Memory Protection.  When MPRV makes a
 * machine-mode load/store behave as supervisor mode, that access is checked by
 * PMP as well as by the page tables.  This helper installs one deliberately
 * broad, unlocked TOR region for the short translation validation sequence.
 *
 * This is not a production security policy.  Future privilege separation will
 * replace it with narrowly scoped regions owned by the security architecture. */
void UmicomRiscvTranslationValidationPmpEnable(void);

/* Remove the temporary broad PMP validation region after the translated
 * load/store checks have finished. */
void UmicomRiscvTranslationValidationPmpDisable(void);

/* Select one Sv39 page-table root in satp and flush stale translation state. */
void UmicomRiscvSv39Activate(UmicomAddress rootTablePhysicalAddress);

/* Disable supervisor address translation and flush stale translation state. */
void UmicomRiscvAddressTranslationDisable(void);

/* Return the current raw satp CSR value for diagnostics and validation. */
UmicomU64 UmicomRiscvSatpRead(void);

/* Read one 64-bit value while the load is evaluated with supervisor privilege.
 *
 * The caller must activate a valid Sv39 hierarchy first. */
UmicomU64 UmicomRiscvLoad64AsSupervisor(UmicomAddress virtualAddress);

/* Write one 64-bit value while the store is evaluated with supervisor
 * privilege.  The caller must activate a writable Sv39 mapping first. */
void UmicomRiscvStore64AsSupervisor(
    UmicomAddress virtualAddress,
    UmicomU64 value
);

#endif /* UMICOM_KERNEL_RISCV64_MMU_H */
