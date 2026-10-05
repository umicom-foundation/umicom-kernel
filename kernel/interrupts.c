/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/interrupts.c
 *
 * PURPOSE:
 *   Own the order in which a hart masks/restores delivery and borrows a timer
 *   source. A refused unlock must never accidentally enable interrupts.
 *
 * EDUCATIONAL OVERVIEW:
 *   Every public operation first masks MIE, then inspects the private records.
 *   Only the last successful Leave restores the outer caller's saved MIE bit.
 *   The controller has one lifetime on hart zero; independent domains would
 *   permit unrelated owners to restore the same hardware bit out of order.
 *
 *   Tokens detect stale or out-of-order calls; they are not security secrets.
 *   Source leases coordinate Kernel services, not arbitrary writes to CSRs.
 *   Existing raw architecture helpers still require disciplined trusted callers.
 *   This code supplies no spin lock, ISR callback routing or SMP exclusion.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/interrupt_state.h"

/* Do not save the complete mstatus as an unlock value. Its other fields can
 * legitimately change at an ECALL, and restoring them can change privilege. */
#define UMICOM_INTERRUPT_MIE ((UmicomU64)8U)
#define UMICOM_INTERRUPT_UNSUPPORTED_STATUS ((UmicomU64)0x26600U)

typedef struct UmicomInterruptScopeRecord {
    UmicomU64 token;
    UmicomU64 owner;
    UmicomU64 savedMie;
} UmicomInterruptScopeRecord;

/* Private static storage stays at one address and is never copied or reset.
 * There is no heap dependency and no caller-held pointer into this state. */
static struct {
    UmicomBoolean initialised;
    UmicomBoolean poisoned;
    UmicomAddress vector;
    UmicomAddress scratch;
    UmicomU64 nextToken;
    UmicomU64 issuedTokens;
    UmicomSize depth;
    UmicomSize highestDepth;
    UmicomU64 expectedSources;
    UmicomU64 expectedDelegation;
    UmicomU64 lease;
    UmicomU64 leaseOwner;
    UmicomU64 leasedSource;
    UmicomInterruptScopeRecord scopes[UMICOM_INTERRUPT_SECTION_LIMIT];
} umicomInterruptController;

static UmicomU64 UmicomInterruptMask(void)
{
    /* The compiler barrier keeps ordinary C accesses on the intended side of
     * the external CSR operation, including under link-time optimisation. */
    __asm__ volatile("" : : : "memory");
    const UmicomU64 prior = UmicomRiscvInterruptMaskSave();
    __asm__ volatile("" : : : "memory");
    return prior;
}

static void UmicomInterruptRestore(UmicomU64 prior)
{
    /* Finish all bookkeeping before enabling a source can cause a trap. The
     * trap may run inside this helper; nothing afterwards changes ownership. */
    __asm__ volatile("" : : : "memory");
    UmicomRiscvInterruptMaskRestore(umicomInterruptController.poisoned != UMICOM_FALSE ? 0U : prior);
    __asm__ volatile("" : : : "memory");
}

static UmicomKernelInterruptStatus UmicomInterruptFinish(
    UmicomKernelInterruptStatus status, UmicomU64 prior)
{
    UmicomInterruptRestore(prior);
    return status;
}

static UmicomKernelInterruptStatus UmicomInterruptCheck(
    UmicomU64 prior, const UmicomRiscvInterruptState *state)
{
    if (umicomInterruptController.initialised == UMICOM_FALSE) return UMICOM_INTERRUPT_NOT_INITIALISED;
    if (umicomInterruptController.poisoned != UMICOM_FALSE) return UMICOM_INTERRUPT_POISONED;

    /* The ordinary vector changes while an ISR or a private user/supervisor
     * return path is active. Such a caller must not join a thread-owned scope.
     * In particular MIE=0 alone cannot prove that this is ordinary Kernel code. */
    const UmicomBoolean normal = state->hart == 0U && state->translation == 0U &&
        (state->status & (UMICOM_INTERRUPT_UNSUPPORTED_STATUS | UMICOM_INTERRUPT_MIE)) == 0U &&
        state->vector == umicomInterruptController.vector &&
        state->scratch == umicomInterruptController.scratch ? UMICOM_TRUE : UMICOM_FALSE;
    if (umicomInterruptController.depth > UMICOM_INTERRUPT_SECTION_LIMIT ||
        (umicomInterruptController.depth != 0U &&
            (normal == UMICOM_FALSE || prior != 0U ||
             state->sources != umicomInterruptController.expectedSources ||
             state->delegation != umicomInterruptController.expectedDelegation))) {
        /* Once an open section lost exclusion or its entry contract, do not
         * "repair" it by re-enabling delivery. Keep records for diagnosis. */
        umicomInterruptController.poisoned = UMICOM_TRUE;
        return UMICOM_INTERRUPT_POISONED;
    }
    return normal != UMICOM_FALSE ? UMICOM_INTERRUPT_OK : UMICOM_INTERRUPT_UNSAFE_CONTEXT;
}

static UmicomKernelInterruptStatus UmicomInterruptNewToken(UmicomU64 *out)
{
    if (umicomInterruptController.nextToken == 0U) return UMICOM_INTERRUPT_TOKEN_EXHAUSTED;
    /* The final value can be issued once. There is no wrap to a stale token. */
    const UmicomU64 token = umicomInterruptController.nextToken;
    umicomInterruptController.nextToken = token == ~(UmicomU64)0U ? 0U : token + 1U;
    umicomInterruptController.issuedTokens = token;
    *out = token;
    return UMICOM_INTERRUPT_OK;
}

static UmicomKernelInterruptStatus UmicomInterruptScopeOwner(UmicomU64 owner)
{
    if (owner == 0U) return UMICOM_INTERRUPT_INVALID_ARGUMENT;
    if (umicomInterruptController.depth == 0U) return UMICOM_INTERRUPT_SECTION_REQUIRED;
    if (umicomInterruptController.scopes[umicomInterruptController.depth - 1U].owner != owner)
        return UMICOM_INTERRUPT_WRONG_OWNER;
    return UMICOM_INTERRUPT_OK;
}

static UmicomKernelInterruptStatus UmicomInterruptLeaseOwner(UmicomU64 owner, UmicomU64 lease)
{
    if (lease == 0U || umicomInterruptController.lease != lease) return UMICOM_INTERRUPT_INVALID_TOKEN;
    if (umicomInterruptController.leaseOwner != owner) return UMICOM_INTERRUPT_WRONG_OWNER;
    return UMICOM_INTERRUPT_OK;
}

UmicomKernelInterruptStatus UmicomKernelInterruptInitialize(
    UmicomAddress expectedVector, UmicomAddress expectedScratch)
{
    const UmicomU64 prior = UmicomInterruptMask();
    if (umicomInterruptController.initialised != UMICOM_FALSE) {
        UmicomRiscvInterruptState existing;
        UmicomRiscvInterruptStateRead(&existing);
        const UmicomKernelInterruptStatus check = UmicomInterruptCheck(prior, &existing);
        return UmicomInterruptFinish(check == UMICOM_INTERRUPT_OK ?
            UMICOM_INTERRUPT_ALREADY_INITIALISED : check, prior);
    }
    if (expectedVector == 0U || (expectedVector & 3U) != 0U ||
        expectedScratch == 0U || (expectedScratch & 15U) != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_ARGUMENT, prior);
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    if (prior != 0U || state.hart != 0U || state.sources != 0U || state.translation != 0U ||
        (state.status & (UMICOM_INTERRUPT_UNSUPPORTED_STATUS | UMICOM_INTERRUPT_MIE)) != 0U ||
        state.vector != expectedVector || state.scratch != expectedScratch)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_UNSAFE_CONTEXT, prior);
    /* Publish only a complete binding. Subsequent operations compare against
     * the installed vector/scratch pair rather than guessing from MPP bits. */
    umicomInterruptController.vector = expectedVector;
    umicomInterruptController.scratch = expectedScratch;
    umicomInterruptController.nextToken = 1U;
    umicomInterruptController.initialised = UMICOM_TRUE;
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, prior);
}

UmicomKernelInterruptStatus UmicomKernelCriticalSectionEnter(
    UmicomU64 owner, UmicomKernelCriticalSection *outSection)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    if (owner == 0U || outSection == (UmicomKernelCriticalSection *)0)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_ARGUMENT, prior);
    if (umicomInterruptController.depth != 0U &&
        umicomInterruptController.scopes[umicomInterruptController.depth - 1U].owner != owner)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_WRONG_OWNER, prior);
    if (umicomInterruptController.depth == UMICOM_INTERRUPT_SECTION_LIMIT)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_DEPTH_LIMIT, prior);
    UmicomU64 token = 0U;
    status = UmicomInterruptNewToken(&token);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    if (umicomInterruptController.depth == 0U) {
        /* Detect uncoordinated source/delegation writes while a section is open.
         * Device pending bits are intentionally not frozen by this comparison. */
        umicomInterruptController.expectedSources = state.sources;
        umicomInterruptController.expectedDelegation = state.delegation;
    }
    UmicomInterruptScopeRecord *const scope = &umicomInterruptController.scopes[umicomInterruptController.depth];
    scope->token = token;
    scope->owner = owner;
    scope->savedMie = prior;
    ++umicomInterruptController.depth;
    if (umicomInterruptController.depth > umicomInterruptController.highestDepth)
        umicomInterruptController.highestDepth = umicomInterruptController.depth;
    *outSection = token;
    /* Success deliberately retains the mask. Only matching Leave may restore it. */
    __asm__ volatile("" : : : "memory");
    return UMICOM_INTERRUPT_OK;
}

UmicomKernelInterruptStatus UmicomKernelCriticalSectionLeave(
    UmicomU64 owner, UmicomKernelCriticalSection section)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    status = UmicomInterruptScopeOwner(owner);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    UmicomInterruptScopeRecord *const top = &umicomInterruptController.scopes[umicomInterruptController.depth - 1U];
    if (section == 0U || top->token != section) {
        /* Distinguish a real outer token from an invalid/stale one. Neither is
         * permitted to pop the top record or restore global delivery. */
        for (UmicomSize index = 0U; index + 1U < umicomInterruptController.depth; ++index)
            if (umicomInterruptController.scopes[index].token == section)
                return UmicomInterruptFinish(UMICOM_INTERRUPT_OUT_OF_ORDER, prior);
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_TOKEN, prior);
    }
    const UmicomU64 restore = top->savedMie;
    top->token = 0U;
    top->owner = 0U;
    top->savedMie = 0U;
    --umicomInterruptController.depth;
    /* Pop before unmask: the deferred ISR must observe a completed unlock. */
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, restore);
}

UmicomKernelInterruptStatus UmicomKernelInterruptSourceAcquire(
    UmicomU64 owner, UmicomU64 source, UmicomKernelInterruptLease *outLease)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status == UMICOM_INTERRUPT_OK) status = UmicomInterruptScopeOwner(owner);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    if (outLease == (UmicomKernelInterruptLease *)0)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_ARGUMENT, prior);
    if (source != UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_UNSUPPORTED, prior);
    if (umicomInterruptController.lease != 0U || (state.delegation & source) != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_BUSY, prior);
    if ((state.sources & source) != 0U) return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_ENABLED, prior);
    if ((state.pending & source) != 0U) return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_PENDING, prior);
    if (UmicomPlatformInterruptSourceQuiescent(source) == UMICOM_FALSE)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_DEVICE_ACTIVE, prior);
    UmicomU64 token = 0U;
    status = UmicomInterruptNewToken(&token);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    umicomInterruptController.lease = token;
    umicomInterruptController.leaseOwner = owner;
    umicomInterruptController.leasedSource = source;
    *outLease = token;
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, prior);
}

static UmicomKernelInterruptStatus UmicomInterruptSourceChange(
    UmicomU64 owner, UmicomU64 lease, UmicomBoolean enable)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status == UMICOM_INTERRUPT_OK) status = UmicomInterruptScopeOwner(owner);
    if (status == UMICOM_INTERRUPT_OK) status = UmicomInterruptLeaseOwner(owner, lease);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    const UmicomU64 source = umicomInterruptController.leasedSource;
    if ((state.delegation & source) != 0U) return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_BUSY, prior);
    const UmicomU64 expected = enable != UMICOM_FALSE ? state.sources | source : state.sources & ~source;
    if (enable != UMICOM_FALSE) UmicomRiscvInterruptSourcesEnable(source);
    else UmicomRiscvInterruptSourcesDisable(source);
    UmicomRiscvInterruptStateRead(&state);
    if (state.sources != expected) {
        /* A WARL refusal or unexpected source write invalidates the ownership
         * contract. Keep delivery masked; do not conceal it with a full CSR write. */
        umicomInterruptController.poisoned = UMICOM_TRUE;
        return UmicomInterruptFinish(UMICOM_INTERRUPT_HARDWARE_REFUSED, 0U);
    }
    umicomInterruptController.expectedSources = expected;
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, prior);
}

UmicomKernelInterruptStatus UmicomKernelInterruptSourceEnable(UmicomU64 owner, UmicomKernelInterruptLease lease)
{
    return UmicomInterruptSourceChange(owner, lease, UMICOM_TRUE);
}
UmicomKernelInterruptStatus UmicomKernelInterruptSourceDisable(UmicomU64 owner, UmicomKernelInterruptLease lease)
{
    return UmicomInterruptSourceChange(owner, lease, UMICOM_FALSE);
}

UmicomKernelInterruptStatus UmicomKernelInterruptSourceRelease(UmicomU64 owner, UmicomKernelInterruptLease lease)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status == UMICOM_INTERRUPT_OK) status = UmicomInterruptScopeOwner(owner);
    if (status == UMICOM_INTERRUPT_OK) status = UmicomInterruptLeaseOwner(owner, lease);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    if ((state.sources & umicomInterruptController.leasedSource) != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_ENABLED, prior);
    if ((state.pending & umicomInterruptController.leasedSource) != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_PENDING, prior);
    if (UmicomPlatformInterruptSourceQuiescent(umicomInterruptController.leasedSource) == UMICOM_FALSE)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_DEVICE_ACTIVE, prior);
    /* The owner, not this generic layer, acknowledged its device. No raw timer
     * value is invented here and no pending interrupt is silently discarded. */
    umicomInterruptController.lease = 0U;
    umicomInterruptController.leaseOwner = 0U;
    umicomInterruptController.leasedSource = 0U;
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, prior);
}

UmicomKernelInterruptStatus UmicomKernelInterruptDeliverySet(UmicomU64 owner, UmicomBoolean enabled)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status != UMICOM_INTERRUPT_OK) return UmicomInterruptFinish(status, prior);
    if (owner == 0U || (enabled != UMICOM_FALSE && enabled != UMICOM_TRUE))
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_ARGUMENT, prior);
    if (umicomInterruptController.depth != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SECTION_ACTIVE, prior);
    if (umicomInterruptController.lease == 0U || umicomInterruptController.leaseOwner != owner)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_WRONG_OWNER, prior);
    if ((state.sources & ~umicomInterruptController.leasedSource) != 0U ||
        (state.delegation & umicomInterruptController.leasedSource) != 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_BUSY, prior);
    if (enabled != UMICOM_FALSE && state.sources == 0U)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_SOURCE_DISABLED, prior);
    /* The requested state is published only after every authority check. */
    return UmicomInterruptFinish(UMICOM_INTERRUPT_OK, enabled != UMICOM_FALSE ? UMICOM_INTERRUPT_MIE : 0U);
}

UmicomKernelInterruptStatus UmicomKernelInterruptSnapshotRead(UmicomKernelInterruptSnapshot *outSnapshot)
{
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    const UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    if (status != UMICOM_INTERRUPT_OK && status != UMICOM_INTERRUPT_POISONED)
        return UmicomInterruptFinish(status, prior);
    if (outSnapshot == (UmicomKernelInterruptSnapshot *)0)
        return UmicomInterruptFinish(UMICOM_INTERRUPT_INVALID_ARGUMENT, prior);
    /* Even a poisoned controller can be diagnosed. This is the one operation
     * which fills its output while returning POISONED; it never clears poison. */
    outSnapshot->depth = umicomInterruptController.depth;
    outSnapshot->sectionOwner = umicomInterruptController.depth == 0U ? 0U : umicomInterruptController.scopes[0].owner;
    outSnapshot->leasedSources = umicomInterruptController.leasedSource;
    outSnapshot->leaseOwner = umicomInterruptController.leaseOwner;
    outSnapshot->enabledSources = state.sources;
    outSnapshot->pendingSources = state.pending;
    outSnapshot->deliveryEnabled = prior != 0U ? UMICOM_TRUE : UMICOM_FALSE;
    outSnapshot->outerDeliveryEnabled = umicomInterruptController.depth != 0U &&
        umicomInterruptController.scopes[0].savedMie != 0U ? UMICOM_TRUE : UMICOM_FALSE;
    outSnapshot->poisoned = umicomInterruptController.poisoned;
    outSnapshot->issuedTokens = umicomInterruptController.issuedTokens;
    outSnapshot->highestDepth = umicomInterruptController.highestDepth;
    return UmicomInterruptFinish(status, prior);
}

UmicomBoolean UmicomKernelInterruptContextSwitchAllowed(void)
{
    /* Existing early tests run before this controller is installed. They have
     * no managed scope or lease; their original machine-state checks still run. */
    if (umicomInterruptController.initialised == UMICOM_FALSE) return UMICOM_TRUE;
    const UmicomU64 prior = UmicomInterruptMask();
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    const UmicomKernelInterruptStatus status = UmicomInterruptCheck(prior, &state);
    const UmicomBoolean allowed = status == UMICOM_INTERRUPT_OK && prior == 0U && state.sources == 0U &&
        umicomInterruptController.depth == 0U && umicomInterruptController.lease == 0U
            ? UMICOM_TRUE : UMICOM_FALSE;
    UmicomInterruptRestore(prior);
    return allowed;
}

const char *UmicomKernelInterruptStatusName(UmicomKernelInterruptStatus status)
{
    /* A bounded explicit vocabulary is useful in serial output and host tests. */
    switch (status) {
        case UMICOM_INTERRUPT_OK: return "ok";
        case UMICOM_INTERRUPT_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_INTERRUPT_NOT_INITIALISED: return "not-initialised";
        case UMICOM_INTERRUPT_ALREADY_INITIALISED: return "already-initialised";
        case UMICOM_INTERRUPT_UNSAFE_CONTEXT: return "unsafe-context";
        case UMICOM_INTERRUPT_WRONG_OWNER: return "wrong-owner";
        case UMICOM_INTERRUPT_INVALID_TOKEN: return "invalid-token";
        case UMICOM_INTERRUPT_OUT_OF_ORDER: return "out-of-order";
        case UMICOM_INTERRUPT_DEPTH_LIMIT: return "depth-limit";
        case UMICOM_INTERRUPT_TOKEN_EXHAUSTED: return "token-exhausted";
        case UMICOM_INTERRUPT_SECTION_REQUIRED: return "section-required";
        case UMICOM_INTERRUPT_SECTION_ACTIVE: return "section-active";
        case UMICOM_INTERRUPT_SOURCE_UNSUPPORTED: return "source-unsupported";
        case UMICOM_INTERRUPT_SOURCE_BUSY: return "source-busy";
        case UMICOM_INTERRUPT_SOURCE_PENDING: return "source-pending";
        case UMICOM_INTERRUPT_SOURCE_ENABLED: return "source-enabled";
        case UMICOM_INTERRUPT_SOURCE_DISABLED: return "source-disabled";
        case UMICOM_INTERRUPT_DEVICE_ACTIVE: return "device-active";
        case UMICOM_INTERRUPT_HARDWARE_REFUSED: return "hardware-refused";
        case UMICOM_INTERRUPT_POISONED: return "poisoned";
        default: return "unknown-interrupt-status";
    }
}

#ifdef UMICOM_INTERRUPT_NATIVE_TESTING
/* These controls exist only in the isolated host test executable. They are
 * never compiled into either Kernel ELF and cannot reset a production owner. */
UmicomSize UmicomKernelInterruptTestDepth(void)
{
    return umicomInterruptController.depth;
}
void UmicomKernelInterruptTestTokenLimit(void)
{
    umicomInterruptController.nextToken = ~(UmicomU64)0U;
}
#endif
