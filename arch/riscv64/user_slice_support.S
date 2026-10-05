/* Umicom Kernel privileged counter observations for a bounded user slice.
 * Keeping CSR reads here lets the native tests substitute hardware operations
 * while compiling the actual C admission/restoration policy.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.umicom_user_slice_support, "ax", @progbits
    .balign 4
    .global UmicomRiscvUserCounterStateRead
    .type UmicomRiscvUserCounterStateRead, @function
UmicomRiscvUserCounterStateRead:
    csrr t0, mcounteren /* Access permissions, not the changing counter values. */
    sd t0, 0(a0)       /* First output belongs to the trusted machine caller. */
    csrr t0, scounteren /* Preserve supervisor-to-user counter permissions too. */
    sd t0, 0(a1)
    ret
    .size UmicomRiscvUserCounterStateRead, . - UmicomRiscvUserCounterStateRead
