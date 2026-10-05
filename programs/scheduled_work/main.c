/*-----------------------------------------------------------------------------
 * Umicom Kernel native scheduling diagnostic
 * File: programs/scheduled_work/main.c
 *
 * PURPOSE:
 *   Keep distinct user registers, nested locals and writable state alive while
 *   timer interrupts hand the CPU to another independently loaded executable.
 *
 * EDUCATIONAL OVERVIEW:
 *   The progress page is private in each address space despite having the same
 *   virtual address. The Kernel's test harness releases both CPU-bound loops
 *   only after observing repeated timer stops. The loop itself makes no call
 *   which could yield voluntarily. This gate is test input, not a new syscall.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_workload.h"
#include "umicom/kernel/user_abi.h"

__attribute__((section(".data.umicom_scheduled_observation")))
static volatile UmicomKernelScheduledObservation umicomScheduledObservation = {
    .identity = 0U, .iterations = 0U, .starts = 0U, .release = 0U,
    .completed = 0U, .localResult = 0U, .magic = 0x554d49434f4dU
};
static const volatile UmicomU8 umicomScheduledLabel[16] = "Umicom context";
UmicomU64 UmicomScheduledSystemCall(UmicomU64 number, UmicomU64 first, UmicomU64 second, UmicomU64 third);
UmicomU64 UmicomScheduledRegisterLoop(volatile UmicomKernelScheduledObservation *observation, UmicomU64 seed);
UmicomU64 UmicomScheduledTemporaryRegisterLoop(volatile UmicomKernelScheduledObservation *observation, UmicomU64 seed);
void UmicomScheduledExitBadStack(void);

__attribute__((noinline)) static UmicomU64 UmicomScheduledNested(UmicomU64 depth, UmicomU64 seed)
{
    /* Volatile locals must survive on the actual user stack, not disappear
     * into a folded expression. Every recursive level spans the timer stops. */
    volatile UmicomU64 first = seed + depth;
    volatile UmicomU64 second = ~(seed + depth);
    UmicomU64 result = 0U;
    if (depth != 0U) {
        result = UmicomScheduledNested(depth - 1U, seed);
    } else {
        /* Alternate the probe's scratch register between the two identities.
         * The peer then explicitly tests the temporary register used here. */
        result = (seed & 256U) != 0U ? UmicomScheduledRegisterLoop(&umicomScheduledObservation, seed) :
            UmicomScheduledTemporaryRegisterLoop(&umicomScheduledObservation, seed);
    }
    if (first != seed + depth || second != ~(seed + depth)) return 0xef10U;
    return result;
}

UmicomU64 UmicomScheduledMain(UmicomU64 argument)
{
    if (umicomScheduledLabel[0] != 'U' || umicomScheduledObservation.starts != 0U ||
        umicomScheduledObservation.magic != 0x554d49434f4dU) return 0xef01U;
    ++umicomScheduledObservation.starts; /* A restart instead of resume is observable. */
    const UmicomU64 identity = UmicomScheduledSystemCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
    umicomScheduledObservation.identity = identity;
    if (identity == 0U) return 0xef02U;
    if (argument == UMICOM_SCHEDULED_OPERATION_FAULT) {
        /* A fault in one task must not discard its neighbour's saved context. */
        volatile UmicomU8 *const readOnly = (volatile UmicomU8 *)(UmicomUIntPtr)umicomScheduledLabel;
        *readOnly = 0U;
        return 0xef03U;
    }
    if (argument == UMICOM_SCHEDULED_OPERATION_BUSY) {
        for (;;) ++umicomScheduledObservation.iterations; /* No ECALL and no voluntary yield. */
    }
    if (argument == UMICOM_SCHEDULED_OPERATION_CALLS) {
        for (;;) (void)UmicomScheduledSystemCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
    }
    if (argument == UMICOM_SCHEDULED_OPERATION_BAD_STACK) {
        UmicomScheduledExitBadStack();
        return 0xef04U;
    }
    const UmicomU64 localResult = UmicomScheduledNested(3U, identity * 256U + argument);
    umicomScheduledObservation.localResult = localResult;
    if (localResult != 0U || umicomScheduledObservation.release != 1U) return 0xef05U;
    /* Resume ordinary services after the asynchronous pauses. The call budget
     * belongs to this task across quanta rather than being reset by dispatch. */
    UmicomU8 copy[16];
    for (UmicomSize byte = 0U; byte < sizeof(copy); ++byte) copy[byte] = 0U;
    if (UmicomScheduledSystemCall(UMICOM_USER_CALL_COPY, (UmicomU64)(UmicomUIntPtr)copy,
        (UmicomU64)(UmicomUIntPtr)umicomScheduledLabel, sizeof(copy)) != UMICOM_USER_RESULT_OK)
        return 0xef06U;
    for (UmicomSize byte = 0U; byte < sizeof(copy); ++byte) {
        if (copy[byte] != umicomScheduledLabel[byte]) return 0xef07U;
    }
    if (UmicomScheduledSystemCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U) != identity) return 0xef08U;
    umicomScheduledObservation.completed = 1U;
    return identity + argument + 0x600U;
}
