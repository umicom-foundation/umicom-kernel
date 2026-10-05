/* Umicom Kernel machine-frame policy tests.
 * These tests use the actual return validator and original C trap dispatcher.
 * Hardware and fatal platform operations are explicit host substitutes; no
 * result from this executable claims that RISC-V Assembly or MRET ran.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/riscv64/trap_integrity.h"
#include "umicom/kernel/platform.h"

static unsigned timerDisabled;
static unsigned sourcesDisabled;
static void Require(int value, const char *reason)
{
    if (value == 0) {
        fprintf(stderr, "FAILED: %s\n", reason);
        exit(1);
    }
}
/* The original cause policy calls these platform hooks. Count their effects
 * rather than substituting another implementation of that policy. */
void UmicomKernelConsoleWrite(const char *text) { (void)text; }
void UmicomKernelConsoleWriteLine(const char *text) { (void)text; }
void UmicomKernelConsoleWriteHex64(UmicomU64 value) { (void)value; }
void UmicomPlatformTimerDisable(UmicomU64 hart) { Require(hart == 0U, "timer hart"); ++timerDisabled; }
void UmicomRiscvMachineTimerInterruptDisable(void) { ++sourcesDisabled; }
UmicomU64 UmicomRiscvReadHartId(void) { return 0U; }
void UmicomPlatformFinishFailure(UmicomU32 code) { fprintf(stderr, "unexpected platform failure %u\n", code); exit(2); }
void UmicomPlatformHalt(void) { exit(3); }

static UmicomRiscvTrapFrame Initial(UmicomKernelTrapRoute route)
{
    UmicomRiscvTrapFrame frame = {0};
    /* Fill distinct bytes so accidentally zeroing any saved GPR is observable. */
    unsigned char *const bytes = (unsigned char *)&frame;
    for (unsigned index = 0U; index < UMICOM_TRAP_INTEGRITY_PC; ++index) {
        bytes[index] = (unsigned char)(index + 1U);
    }
    frame.mepc = 0x1102U; /* Halfword-aligned ECALL is valid with compressed ISA. */
    frame.mstatus = 0x1880U;
    frame.mcause = route == UMICOM_TRAP_ROUTE_MACHINE_ECALL ? 11U :
        route == UMICOM_TRAP_ROUTE_MACHINE_TIMER ? (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U) :
        route == UMICOM_TRAP_ROUTE_PROBE_LOAD ? 13U : 15U;
    if (route == UMICOM_TRAP_ROUTE_PROBE_LOAD || route == UMICOM_TRAP_ROUTE_PROBE_STORE) {
        frame.mstatus |= 0x20000U;
    }
    frame.mtval = 0x100000U;
    return frame;
}
static UmicomRiscvTrapFrame Completed(UmicomRiscvTrapFrame frame, UmicomKernelTrapRoute route)
{
    if (route != UMICOM_TRAP_ROUTE_MACHINE_TIMER) frame.mepc += 4U;
    if (route == UMICOM_TRAP_ROUTE_MACHINE_TIMER) frame.mstatus &= ~(UmicomU64)0x80U;
    if (route == UMICOM_TRAP_ROUTE_PROBE_LOAD || route == UMICOM_TRAP_ROUTE_PROBE_STORE)
        frame.mstatus &= ~(UmicomU64)0x20000U;
    return frame;
}
static UmicomKernelTrapIntegrityStatus Check(const UmicomRiscvTrapFrame *before,
    const UmicomRiscvTrapFrame *after, UmicomKernelTrapRoute route)
{
    return UmicomKernelTrapReturnCheck(before, after, route, 0x1000U, 0x2000U);
}
int main(int argc, char **argv)
{
    Require(argc == 2, "one named case required");
    const char *const name = argv[1];
    UmicomKernelTrapRoute route = UMICOM_TRAP_ROUTE_MACHINE_ECALL;
    if (strcmp(name, "timer") == 0 || strcmp(name, "timer-missing-interrupt") == 0 ||
        strcmp(name, "timer-mpie-not-cleared") == 0 || strcmp(name, "original-timer-policy") == 0)
        route = UMICOM_TRAP_ROUTE_MACHINE_TIMER;
    if (strcmp(name, "load-probe") == 0 || strcmp(name, "probe-without-mprv") == 0 ||
        strcmp(name, "probe-mprv-not-cleared") == 0) route = UMICOM_TRAP_ROUTE_PROBE_LOAD;
    if (strcmp(name, "store-probe") == 0) route = UMICOM_TRAP_ROUTE_PROBE_STORE;
    UmicomRiscvTrapFrame before = Initial(route);
    UmicomRiscvTrapFrame after = Completed(before, route);
    UmicomKernelTrapIntegrityStatus expected = UMICOM_TRAP_INTEGRITY_OK;
    if (strncmp(name, "register-", 9U) == 0) {
        const unsigned long reg = strtoul(name + 9, NULL, 10);
        Require(reg >= 1U && reg <= 31U, "register range");
        ((unsigned char *)&after)[(reg - 1U) * 8U] ^= 1U;
        expected = UMICOM_TRAP_INTEGRITY_CHANGED_REGISTERS;
    } else if (strcmp(name, "null-before") == 0) {
        Require(Check(NULL, &after, route) == UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT, name); return 0;
    } else if (strcmp(name, "null-after") == 0) {
        Require(Check(&before, NULL, route) == UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT, name); return 0;
    } else if (strcmp(name, "empty-text") == 0) {
        Require(UmicomKernelTrapFrameCheck(&before, route, 1U, 1U) == UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT, name); return 0;
    } else if (strcmp(name, "invalid-route") == 0) {
        Require(Check(&before, &after, (UmicomKernelTrapRoute)99) == UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT, name); return 0;
    } else if (strncmp(name, "privilege-", 10U) == 0) {
        const unsigned long privilege = strtoul(name + 10, NULL, 10);
        before.mstatus = (before.mstatus & ~(UmicomU64)0x1800U) | ((UmicomU64)privilege << 11U);
        expected = UMICOM_TRAP_INTEGRITY_WRONG_PRIVILEGE;
    } else if (strcmp(name, "live-mie") == 0) {
        before.mstatus |= 8U; expected = UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    } else if (strcmp(name, "floating-state") == 0) {
        before.mstatus |= 0x2000U; expected = UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    } else if (strcmp(name, "vector-state") == 0) {
        before.mstatus |= 0x200U; expected = UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    } else if (strcmp(name, "ordinary-mprv") == 0) {
        before.mstatus |= 0x20000U; expected = UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    } else if (strcmp(name, "probe-without-mprv") == 0) {
        before.mstatus &= ~(UmicomU64)0x20000U; expected = UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    } else if (strcmp(name, "unaligned-pc") == 0) {
        before.mepc |= 1U; expected = UMICOM_TRAP_INTEGRITY_INVALID_PC;
    } else if (strcmp(name, "pc-below-text") == 0) {
        before.mepc = 0xffeU; expected = UMICOM_TRAP_INTEGRITY_INVALID_PC;
    } else if (strcmp(name, "pc-after-text") == 0) {
        before.mepc = 0x2000U; expected = UMICOM_TRAP_INTEGRITY_INVALID_PC;
    } else if (strcmp(name, "pc-at-last-instruction") == 0) {
        before.mepc = 0x1ffcU; expected = UMICOM_TRAP_INTEGRITY_INVALID_PC;
    } else if (strcmp(name, "pc-overflow") == 0) {
        before.mepc = ~(UmicomU64)0U - 3U;
        Require(UmicomKernelTrapFrameCheck(&before, route, ~(UmicomAddress)0U - 31U,
            ~(UmicomAddress)0U) == UMICOM_TRAP_INTEGRITY_INVALID_PC, name); return 0;
    } else if (strcmp(name, "user-ecall-cause") == 0) {
        before.mcause = 8U; expected = UMICOM_TRAP_INTEGRITY_WRONG_CAUSE;
    } else if (strcmp(name, "supervisor-ecall-cause") == 0) {
        before.mcause = 9U; expected = UMICOM_TRAP_INTEGRITY_WRONG_CAUSE;
    } else if (strcmp(name, "unknown-cause") == 0) {
        before.mcause = 2U; expected = UMICOM_TRAP_INTEGRITY_WRONG_CAUSE;
    } else if (strcmp(name, "timer-missing-interrupt") == 0) {
        before.mcause = 7U; expected = UMICOM_TRAP_INTEGRITY_WRONG_CAUSE;
    } else if (strcmp(name, "changed-cause") == 0) {
        after.mcause = 2U; expected = UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS;
    } else if (strcmp(name, "changed-trap-value") == 0) {
        ++after.mtval; expected = UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS;
    } else if (strcmp(name, "changed-reserved") == 0) {
        after.reserved = 1U; expected = UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS;
    } else if (strcmp(name, "repeated-ecall") == 0) {
        after.mepc = before.mepc; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_PC;
    } else if (strcmp(name, "wrong-instruction-length") == 0) {
        after.mepc = before.mepc + 2U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_PC;
    } else if (strcmp(name, "return-enables-mie") == 0) {
        after.mstatus |= 8U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    } else if (strcmp(name, "return-changes-mpp") == 0) {
        after.mstatus &= ~(UmicomU64)0x1800U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    } else if (strcmp(name, "return-enables-mprv") == 0) {
        after.mstatus |= 0x20000U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    } else if (strcmp(name, "timer-mpie-not-cleared") == 0) {
        after.mstatus |= 0x80U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    } else if (strcmp(name, "probe-mprv-not-cleared") == 0) {
        after.mstatus |= 0x20000U; expected = UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    } else if (strcmp(name, "zero-interrupted-stack") == 0) {
        before.x2_sp = 0U; after.x2_sp = 0U;
    } else if (strcmp(name, "unaligned-interrupted-stack") == 0) {
        before.x2_sp = 3U; after.x2_sp = 3U;
    } else if (strcmp(name, "original-ecall-policy") == 0 || strcmp(name, "original-timer-policy") == 0) {
        after = before;
        UmicomRiscvTrapDispatch(&after);
        UmicomRiscvTrapSnapshot snapshot = {0};
        UmicomRiscvTrapSnapshotRead(&snapshot);
        if (route == UMICOM_TRAP_ROUTE_MACHINE_TIMER)
            Require(timerDisabled == 1U && sourcesDisabled == 1U && snapshot.timerInterruptCount == 1U, name);
        else Require(snapshot.exceptionCount == 1U && timerDisabled == 0U, name);
    } else {
        Require(strcmp(name, "ecall") == 0 || strcmp(name, "timer") == 0 ||
            strcmp(name, "load-probe") == 0 || strcmp(name, "store-probe") == 0, "known case");
    }
    const UmicomKernelTrapIntegrityStatus result = Check(&before, &after, route);
    if (result != expected) fprintf(stderr, "%s: got %s expected %s\n", name,
        UmicomKernelTrapIntegrityStatusName(result), UmicomKernelTrapIntegrityStatusName(expected));
    Require(result == expected, name);
    printf("PASS %s\n", name);
    return 0;
}
