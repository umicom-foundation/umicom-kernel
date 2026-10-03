/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_payload.c
 *
 * PURPOSE:
 *   Exercise real U-mode calls and protection faults using only the pages that
 *   the Kernel deliberately maps for this small built-in program.
 *
 * EDUCATIONAL OVERVIEW:
 *   This file has no console, allocator, device access or Kernel data pointer.
 *   Its only privileged service path is ECALL. The same executable pages are
 *   shared read-only by two address spaces; their stack and data frames differ.
 *   The Kernel separately checks traps, bytes, control state and ownership, so
 *   a user-written "completed" flag is never enough to declare a test passed.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/user_execution.h"
#include "umicom/kernel/user_observation.h"

/* Place every instruction in the user-only range selected by the linker.
 * No switch table or external constant pool may depend on machine-only rodata. */
__attribute__((section(".text.umicom_user_payload"), noinline))
UmicomU64 UmicomRiscvUserPayload(UmicomU64 operation)
{
    /* These addresses are identical across tasks, but name different frames. */
    volatile UmicomKernelUserObservation *const observation =
        (volatile UmicomKernelUserObservation *)(UmicomAddress)UMICOM_USER_DATA_BASE;
    const UmicomU64 identity = UmicomRiscvUserSystemCall(
        UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
    observation->identity = identity;
    observation->completed = 0U;

    /* Volatile locals force real accesses to the translated writable user stack. */
    volatile UmicomU64 values[4];
    values[0] = identity;
    values[1] = identity + 1U;
    values[2] = identity + 2U;
    values[3] = identity + 3U;
    observation->stackResult = values[0] + values[1] + values[2] + values[3];
    UmicomAddress stack = 0U;
    __asm__ volatile("mv %0, sp" : "=r"(stack));
    observation->stackAddress = stack;

    /* Fault cases use labelled instructions so the machine owner can check the
     * actual fault PC, not just match a generic failure message. */
    if (operation == UMICOM_USER_OPERATION_GUARD_LOAD) {
        (void)UmicomRiscvUserLoad(UMICOM_USER_STACK_BASE - UMICOM_USER_PAGE_BYTES);
        return 101U;
    }
    if (operation == UMICOM_USER_OPERATION_READONLY_STORE) {
        UmicomRiscvUserStore(UMICOM_USER_READONLY_BASE, 0U);
        return 102U;
    }
    if (operation == UMICOM_USER_OPERATION_SUPERVISOR_LOAD) {
        (void)UmicomRiscvUserLoad(UMICOM_USER_SUPERVISOR_BASE);
        return 103U;
    }
    if (operation == UMICOM_USER_OPERATION_SUPERVISOR_CSR) {
        (void)UmicomRiscvUserSupervisorCsrRead();
        return 104U;
    }
    if (operation == UMICOM_USER_OPERATION_NONEXECUTABLE) {
        UmicomRiscvUserJump(UMICOM_USER_DATA_BASE);
        return 105U;
    }
    if (operation == UMICOM_USER_OPERATION_INVALID_STACK_EXIT) {
        UmicomRiscvUserExitWithInvalidStack(UMICOM_USER_STACK_BASE - UMICOM_USER_PAGE_BYTES);
        return 106U;
    }
    if (operation == UMICOM_USER_OPERATION_CALL_BUDGET) {
        for (;;) {
            (void)UmicomRiscvUserSystemCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
        }
    }
    if (operation == UMICOM_USER_OPERATION_BUSY_LOOP) {
        UmicomRiscvUserBusyLoop();
        return 107U;
    }
    if (operation != UMICOM_USER_OPERATION_NORMAL) {
        return 108U;
    }

    observation->syscallChecks = 0U;
    observation->registerCheck = UmicomRiscvUserRegisterProbe();
    if (observation->registerCheck != 1U) {
        return 109U;
    }

    /* The source straddles two user pages backed by separately owned frames. */
    const UmicomAddress source = UMICOM_USER_DATA_BASE + UMICOM_USER_PAGE_BYTES - 8U;
    const UmicomAddress destination = UMICOM_USER_DATA_BASE + 128U;
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination, source, 16U) !=
        UMICOM_USER_RESULT_OK) {
        return 110U;
    }
    for (UmicomSize index = 0U; index < 16U; ++index) {
        if (*(const volatile UmicomU8 *)(destination + index) != (UmicomU8)(0x40U + index)) {
            return 111U;
        }
    }
    ++observation->syscallChecks;

    /* Unsupported calls are refused and resumed; they do not crash the Kernel. */
    if (UmicomRiscvUserSystemCall(99U, 0U, 0U, 0U) != UMICOM_USER_RESULT_UNKNOWN_CALL) {
        return 112U;
    }
    ++observation->syscallChecks;
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination,
            UMICOM_USER_STACK_BASE - UMICOM_USER_PAGE_BYTES, 8U) != UMICOM_USER_RESULT_BAD_ADDRESS) {
        return 113U;
    }
    ++observation->syscallChecks;

    /* A present supervisor-only mapping is still forbidden to a user copy. */
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination,
            UMICOM_USER_SUPERVISOR_BASE, 8U) != UMICOM_USER_RESULT_DENIED) {
        return 114U;
    }
    ++observation->syscallChecks;
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, UMICOM_USER_READONLY_BASE,
            source, 8U) != UMICOM_USER_RESULT_DENIED) {
        return 115U;
    }
    ++observation->syscallChecks;

    /* A wrapping span must be refused before address arithmetic or memory I/O. */
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination,
            ~(UmicomU64)0U - 3U, 8U) != UMICOM_USER_RESULT_BAD_ADDRESS) {
        return 116U;
    }
    ++observation->syscallChecks;

    /* The first eight destination bytes exist but the next page does not.
     * None of those first eight bytes may change on this rejected request. */
    const UmicomAddress partial = UMICOM_USER_DATA_BASE + 2U * UMICOM_USER_PAGE_BYTES - 8U;
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, partial, source, 16U) !=
        UMICOM_USER_RESULT_BAD_ADDRESS) {
        return 117U;
    }
    for (UmicomSize index = 0U; index < 8U; ++index) {
        if (*(const volatile UmicomU8 *)(partial + index) != (UmicomU8)0xccU) {
            return 118U;
        }
    }
    ++observation->syscallChecks;
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination, source,
            UMICOM_USER_COPY_LIMIT + 1U) != UMICOM_USER_RESULT_TOO_LARGE) {
        return 119U;
    }
    ++observation->syscallChecks;

    /* Bit 38 without its upper sign extension is not a canonical Sv39 address. */
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, destination,
            0x0000004000000000ULL, 8U) != UMICOM_USER_RESULT_BAD_ADDRESS) {
        return 120U;
    }
    ++observation->syscallChecks;

    /* The documented zero-length rule does not dereference either pointer. */
    if (UmicomRiscvUserSystemCall(UMICOM_USER_CALL_COPY, 0U, 0U, 0U) !=
        UMICOM_USER_RESULT_OK) {
        return 121U;
    }
    ++observation->syscallChecks;

    /* This counter belongs to one address space. Running another task with the
     * same virtual address must not increment or overwrite it. */
    ++observation->runs;
    observation->completed = 1U;
    return 0U;
}
