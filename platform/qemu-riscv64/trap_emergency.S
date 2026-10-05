/* Umicom Kernel QEMU last-resort stop.
 * No stack or scratch pointer is trusted here. If even emergency diagnostics
 * trap, attempt the documented test-finisher write once and otherwise park.
 * Device failure can still prevent emulator termination; CTest has a timeout.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .section .text.umicom_trap_last_resort, "ax", @progbits
    .balign 16
    .global UmicomPlatformTrapEmergencyHalt
    .type UmicomPlatformTrapEmergencyHalt, @function
UmicomPlatformTrapEmergencyHalt:
    /* Redirect any fault in the finisher access to a stackless parking loop. */
    la t0, .Lumicom_trap_park
    csrw mtvec, t0
    csrw mie, zero
    li t0, 0x20008
    csrc mstatus, t0
    li t0, 0x00100000       /* QEMU virt SiFive test-finisher address. */
    li t1, 0x002b3333       /* Distinct failed-test code, never success. */
    sw t1, 0(t0)
    .balign 16
.Lumicom_trap_park:
    wfi                    /* If the device is absent, do not execute unrelated code. */
    j .Lumicom_trap_park
    .size UmicomPlatformTrapEmergencyHalt, . - UmicomPlatformTrapEmergencyHalt
