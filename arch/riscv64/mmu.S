/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/mmu.S
 *
 * PURPOSE:
 *   Provide the small privileged-instruction layer needed to prove real Sv39
 *   hardware translation while the main Umicom Kernel still executes in
 *   machine mode.
 *
 * EDUCATIONAL OVERVIEW:
 *   The Kernel already constructs Sv39 page tables in C23.  This file adds the
 *   few operations that genuinely require privileged RISC-V instructions:
 *
 *     - configuring temporary PMP access for supervisor-effective validation;
 *     - writing and reading the `satp` translation-control CSR;
 *     - flushing stale TLB/page-walk state with `sfence.vma`;
 *     - performing one load or store with `mstatus.MPRV=1` and MPP=S.
 *
 *   MPRV affects explicit data accesses only.  It does not change the current
 *   execution privilege and does not make instruction fetch use supervisor
 *   translation.  That property lets us validate the page tables without
 *   risking the current Kernel instruction stream or stack.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

    .section .text.mmu, "ax", @progbits
    .align 4

    /* mstatus.MPP occupies bits 12:11. */
    .equ UMICOM_RISCV_MSTATUS_MPP_MASK, 0x1800

    /* MPP=01 selects supervisor mode as the effective privilege. */
    .equ UMICOM_RISCV_MSTATUS_MPP_SUPERVISOR, 0x0800

    /* mstatus.MPRV is bit 17. */
    .equ UMICOM_RISCV_MSTATUS_MPRV, 0x20000

    /* satp.MODE=8 selects Sv39 on RV64. */
    .equ UMICOM_RISCV_SATP_MODE_SV39, 8

    /*-------------------------------------------------------------------------
     * UmicomRiscvTranslationValidationPmpEnable
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvTranslationValidationPmpEnable
    .type UmicomRiscvTranslationValidationPmpEnable, @function

UmicomRiscvTranslationValidationPmpEnable:
    /* A TOR region uses pmpaddr0 as the exclusive upper boundary.  Writing all
     * ones gives the validation region the widest representable upper limit. */
    li t0, -1
    csrw pmpaddr0, t0

    /* pmpcfg0 byte zero:
     *
     *   bit 0 R = 1
     *   bit 1 W = 1
     *   bit 2 X = 1
     *   bits 4:3 A = 01 (TOR)
     *   bit 7 L = 0 (unlocked)
     *
     * 0x0f therefore grants read/write/execute access from address zero up to
     * the pmpaddr0 TOR boundary.  This broad policy exists only so MPRV-based
     * translation validation is not blocked by an unrelated empty PMP policy. */
    li t0, 0x0f
    csrw pmpcfg0, t0

    /* Return after PMP state is fully established. */
    ret

    .size UmicomRiscvTranslationValidationPmpEnable, . - UmicomRiscvTranslationValidationPmpEnable


    /*-------------------------------------------------------------------------
     * UmicomRiscvTranslationValidationPmpDisable
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvTranslationValidationPmpDisable
    .type UmicomRiscvTranslationValidationPmpDisable, @function

UmicomRiscvTranslationValidationPmpDisable:
    /* The validation entry is deliberately unlocked, so clearing its
     * configuration byte removes the temporary supervisor access policy. */
    csrw pmpcfg0, zero

    /* Clear the associated TOR boundary as well so later debugging cannot
     * mistake a stale address value for an active region. */
    csrw pmpaddr0, zero

    /* Return to ordinary machine-mode Kernel execution. */
    ret

    .size UmicomRiscvTranslationValidationPmpDisable, . - UmicomRiscvTranslationValidationPmpDisable


    /*-------------------------------------------------------------------------
     * UmicomRiscvSv39Activate
     *
     * a0 = physical address of the root page-table frame.
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvSv39Activate
    .type UmicomRiscvSv39Activate, @function

UmicomRiscvSv39Activate:
    /* satp stores the root physical page number rather than a byte address.
     * Page tables are 4 KiB aligned, so dividing by 4096 is a twelve-bit shift. */
    srli t1, a0, 12

    /* Construct MODE=Sv39 in satp bits 63:60. */
    li t0, UMICOM_RISCV_SATP_MODE_SV39
    slli t0, t0, 60

    /* ASID remains zero for this first address-space implementation.  Combine
     * the MODE field with the root physical page number. */
    or t0, t0, t1

    /* Make the selected Sv39 hierarchy visible to supervisor translation. */
    csrw satp, t0

    /* Page-table changes or a satp change can leave stale translation-cache
     * state.  A global fence is simple and correct while only one hart is active. */
    sfence.vma zero, zero

    /* Return with machine-mode instruction fetch still using physical addresses. */
    ret

    .size UmicomRiscvSv39Activate, . - UmicomRiscvSv39Activate


    /*-------------------------------------------------------------------------
     * UmicomRiscvAddressTranslationDisable
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvAddressTranslationDisable
    .type UmicomRiscvAddressTranslationDisable, @function

UmicomRiscvAddressTranslationDisable:
    /* satp.MODE=0 (the all-zero CSR value) selects Bare translation. */
    csrw satp, zero

    /* Discard any translations associated with the hierarchy that was active. */
    sfence.vma zero, zero

    /* Return to a fully untranslated supervisor-address configuration. */
    ret

    .size UmicomRiscvAddressTranslationDisable, . - UmicomRiscvAddressTranslationDisable


    /*-------------------------------------------------------------------------
     * UmicomRiscvSatpRead
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvSatpRead
    .type UmicomRiscvSatpRead, @function

UmicomRiscvSatpRead:
    /* The normal integer return register is a0. */
    csrr a0, satp

    /* Return the raw CSR value so C23 diagnostics can inspect MODE/root PPN. */
    ret

    .size UmicomRiscvSatpRead, . - UmicomRiscvSatpRead


    /*-------------------------------------------------------------------------
     * UmicomRiscvLoad64AsSupervisor
     *
     * a0 = virtual address to read.
     * returns a0 = translated 64-bit value.
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvLoad64AsSupervisor
    .type UmicomRiscvLoad64AsSupervisor, @function

UmicomRiscvLoad64AsSupervisor:
    /* Preserve the complete caller mstatus value so MPRV/MPP changes are
     * temporary and cannot leak into ordinary machine-mode Kernel work. */
    csrr t0, mstatus

    /* Start by clearing the two-bit MPP field. */
    li t1, UMICOM_RISCV_MSTATUS_MPP_MASK
    csrc mstatus, t1

    /* Select supervisor mode in MPP. */
    li t1, UMICOM_RISCV_MSTATUS_MPP_SUPERVISOR
    csrs mstatus, t1

    /* Enable MPRV.  The following explicit load now uses supervisor privilege,
     * so satp/Sv39 and PMP participate in address translation and permission checks. */
    li t1, UMICOM_RISCV_MSTATUS_MPRV
    csrs mstatus, t1

    /* This is the operation under test: read through the virtual address. */
    ld t2, 0(a0)

    /* Restore the exact machine-status value that existed before validation. */
    csrw mstatus, t0

    /* Move the translated value into the standard integer return register. */
    mv a0, t2

    /* Return to ordinary machine-mode C23 code. */
    ret

    .size UmicomRiscvLoad64AsSupervisor, . - UmicomRiscvLoad64AsSupervisor


    /*-------------------------------------------------------------------------
     * UmicomRiscvStore64AsSupervisor
     *
     * a0 = virtual address to write.
     * a1 = 64-bit value to store.
     *-------------------------------------------------------------------------*/
    .global UmicomRiscvStore64AsSupervisor
    .type UmicomRiscvStore64AsSupervisor, @function

UmicomRiscvStore64AsSupervisor:
    /* Preserve the caller's full mstatus before changing effective privilege. */
    csrr t0, mstatus

    /* Clear the current MPP field. */
    li t1, UMICOM_RISCV_MSTATUS_MPP_MASK
    csrc mstatus, t1

    /* Select supervisor mode as the data access privilege. */
    li t1, UMICOM_RISCV_MSTATUS_MPP_SUPERVISOR
    csrs mstatus, t1

    /* Enable MPRV so the next store uses supervisor translation/permissions. */
    li t1, UMICOM_RISCV_MSTATUS_MPRV
    csrs mstatus, t1

    /* Write through the mapped virtual address. */
    sd a1, 0(a0)

    /* Restore machine status immediately after the translated store. */
    csrw mstatus, t0

    /* Return to the caller with no translated-access privilege left active. */
    ret

    .size UmicomRiscvStore64AsSupervisor, . - UmicomRiscvStore64AsSupervisor
