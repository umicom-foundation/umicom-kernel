/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/interrupts.h
 *
 * PURPOSE:
 *   Give hart-local interrupt masking an explicit owner, nesting order and
 *   lifetime, and prevent a timer source from being borrowed by two services.
 *
 * EDUCATIONAL OVERVIEW:
 *   Masking an interrupt does not acknowledge its device. A timer can become
 *   pending while MIE is clear, then arrive as soon as the outermost section
 *   restores MIE. We therefore restore only that bit, not an old copy of every
 *   privilege-control field in mstatus.
 *
 *   These are trusted machine-mode operations for one hart, not SMP locks or
 *   user capabilities. The owner is a stable execution-context identity chosen
 *   by Kernel code. Do not change stacks, enter user mode or wait while holding
 *   a section or a source lease. The normal thread/process entry paths check
 *   that rule; direct architecture primitives remain privileged internal APIs.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_INTERRUPTS_H
#define UMICOM_KERNEL_INTERRUPTS_H
#include "umicom/kernel/types.h"

/* Exhaustion is a refused operation, not an implicit unlock or a wrapped token. */
#define UMICOM_INTERRUPT_SECTION_LIMIT 16U
#define UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER ((UmicomU64)0x80U)
typedef UmicomU64 UmicomKernelCriticalSection;
typedef UmicomU64 UmicomKernelInterruptLease;

typedef enum UmicomKernelInterruptStatus {
    UMICOM_INTERRUPT_OK,
    UMICOM_INTERRUPT_INVALID_ARGUMENT,
    UMICOM_INTERRUPT_NOT_INITIALISED,
    UMICOM_INTERRUPT_ALREADY_INITIALISED,
    UMICOM_INTERRUPT_UNSAFE_CONTEXT,
    UMICOM_INTERRUPT_WRONG_OWNER,
    UMICOM_INTERRUPT_INVALID_TOKEN,
    UMICOM_INTERRUPT_OUT_OF_ORDER,
    UMICOM_INTERRUPT_DEPTH_LIMIT,
    UMICOM_INTERRUPT_TOKEN_EXHAUSTED,
    UMICOM_INTERRUPT_SECTION_REQUIRED,
    UMICOM_INTERRUPT_SECTION_ACTIVE,
    UMICOM_INTERRUPT_SOURCE_UNSUPPORTED,
    UMICOM_INTERRUPT_SOURCE_BUSY,
    UMICOM_INTERRUPT_SOURCE_PENDING,
    UMICOM_INTERRUPT_SOURCE_ENABLED,
    UMICOM_INTERRUPT_SOURCE_DISABLED,
    UMICOM_INTERRUPT_DEVICE_ACTIVE,
    UMICOM_INTERRUPT_HARDWARE_REFUSED,
    UMICOM_INTERRUPT_POISONED
} UmicomKernelInterruptStatus;

/* A value snapshot, never a pointer into the controller's ownership records.
 * pendingSources is an observation and can change independently in hardware. */
typedef struct UmicomKernelInterruptSnapshot {
    UmicomSize depth;
    UmicomU64 sectionOwner;
    UmicomU64 leasedSources;
    UmicomU64 leaseOwner;
    UmicomU64 enabledSources;
    UmicomU64 pendingSources;
    UmicomBoolean deliveryEnabled;
    UmicomBoolean outerDeliveryEnabled;
    UmicomBoolean poisoned;
    UmicomU64 issuedTokens;
    UmicomSize highestDepth;
} UmicomKernelInterruptSnapshot;

/* Call once on hart zero with MIE and mie clear, satp Bare and the ordinary
 * machine vector/landing stack already installed. There is one controller for
 * this hart: independent masking domains could otherwise restore MIE out of
 * order. expectedVector and expectedScratch are trusted, installed addresses.
 * A live controller is never reset, even after all its resources are released. */
UmicomKernelInterruptStatus UmicomKernelInterruptInitialize(
    UmicomAddress expectedVector, UmicomAddress expectedScratch);

/* Enter atomically saves and clears MIE before touching the nesting records.
 * Nested callers must have the same owner and leave in reverse acquisition
 * order. Output tokens are unchanged on refusal. Kernel output pointers must
 * be valid and must not overlap controller storage. Tokens are not secrets. */
UmicomKernelInterruptStatus UmicomKernelCriticalSectionEnter(
    UmicomU64 owner, UmicomKernelCriticalSection *outSection);
UmicomKernelInterruptStatus UmicomKernelCriticalSectionLeave(
    UmicomU64 owner, UmicomKernelCriticalSection section);

/* Source operations require a section belonging to the same owner. Only the
 * machine timer is supported because that is the source whose existing entry
 * and acknowledgement path have been implemented. No PLIC or new ISR routing
 * is implied. Acquire requires the source disabled, nondelegated and not pending.
 * Release also requires disabled/not-pending and a quiesced device. The QEMU
 * timer must be parked at its maximum compare value, not a near-future deadline.
 * Masking cannot erase device work or cancel an already programmed deadline. */
UmicomKernelInterruptStatus UmicomKernelInterruptSourceAcquire(
    UmicomU64 owner, UmicomU64 source, UmicomKernelInterruptLease *outLease);
UmicomKernelInterruptStatus UmicomKernelInterruptSourceEnable(
    UmicomU64 owner, UmicomKernelInterruptLease lease);
UmicomKernelInterruptStatus UmicomKernelInterruptSourceDisable(
    UmicomU64 owner, UmicomKernelInterruptLease lease);
UmicomKernelInterruptStatus UmicomKernelInterruptSourceRelease(
    UmicomU64 owner, UmicomKernelInterruptLease lease);

/* Explicit outer delivery policy, never an unconditional unlock. No section
 * may be open. Enabling requires the caller to own every enabled source and
 * at least one source to be enabled. This changes MIE only; the existing timer
 * handler may subsequently acknowledge its one-shot source and clear MIE. */
UmicomKernelInterruptStatus UmicomKernelInterruptDeliverySet(
    UmicomU64 owner, UmicomBoolean enabled);
UmicomKernelInterruptStatus UmicomKernelInterruptSnapshotRead(
    UmicomKernelInterruptSnapshot *outSnapshot);

/* Used by the established thread/process readiness checks. Before controller
 * initialisation there is no lease to honour. After initialisation, a section,
 * lease or poisoned state prevents switching. Live MIE/mie also refuse a
 * switch. This predicate does not allocate or schedule a thread itself. */
UmicomBoolean UmicomKernelInterruptContextSwitchAllowed(void);
const char *UmicomKernelInterruptStatusName(UmicomKernelInterruptStatus status);
void UmicomKernelInterruptOwnershipValidate(void);
#endif /* UMICOM_KERNEL_INTERRUPTS_H */
