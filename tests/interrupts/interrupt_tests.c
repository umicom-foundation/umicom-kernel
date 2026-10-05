/*-----------------------------------------------------------------------------
 * Umicom Kernel native interrupt-ownership tests
 *
 * Every invocation gets a fresh controller lifetime. Hardware registers are
 * explicitly modelled; the tested ownership and board quiescence code are the
 * real implementation. Refusals must not silently restore MIE or consume tokens.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); \
} } while (0)
#define OK(expression) CHECK((expression) == UMICOM_INTERRUPT_OK)

static void Initialise(void)
{
    OK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230000U));
}
static UmicomKernelCriticalSection Enter(UmicomU64 owner)
{
    UmicomKernelCriticalSection token = 0U;
    OK(UmicomKernelCriticalSectionEnter(owner, &token));
    CHECK(token != 0U && (umicomInterruptModel.state.status & 8U) == 0U);
    return token;
}
static UmicomKernelInterruptLease Acquire(void)
{
    UmicomKernelInterruptLease token = 0U;
    OK(UmicomKernelInterruptSourceAcquire(11U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &token));
    CHECK(token != 0U);
    return token;
}
static UmicomKernelInterruptSnapshot Snapshot(void)
{
    UmicomKernelInterruptSnapshot snapshot;
    OK(UmicomKernelInterruptSnapshotRead(&snapshot));
    return snapshot;
}
static void Leave(UmicomKernelCriticalSection section)
{
    OK(UmicomKernelCriticalSectionLeave(11U, section));
}

static void Early(const char *name)
{
    UmicomKernelCriticalSection token = 91U;
    if (strcmp(name, "uninitialised") == 0) {
        CHECK(UmicomKernelCriticalSectionEnter(11U, &token) == UMICOM_INTERRUPT_NOT_INITIALISED);
        CHECK(token == 91U); return;
    }
    if (strcmp(name, "switch-before-initialise") == 0) {
        CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_TRUE); return;
    }
    if (strcmp(name, "initialise-enabled") == 0) umicomInterruptModel.state.status = 8U;
    else if (strcmp(name, "initialise-sources") == 0) umicomInterruptModel.state.sources = 0x80U;
    else if (strcmp(name, "initialise-hart") == 0) umicomInterruptModel.state.hart = 1U;
    else if (strcmp(name, "initialise-translation") == 0) umicomInterruptModel.state.translation = 1U;
    else if (strcmp(name, "initialise-mprv") == 0) umicomInterruptModel.state.status = 0x20000U;
    else if (strcmp(name, "initialise-vector") == 0) {
        CHECK(UmicomKernelInterruptInitialize(0x80200101U, 0x80230000U) == UMICOM_INTERRUPT_INVALID_ARGUMENT); return;
    } else if (strcmp(name, "initialise-scratch") == 0) {
        CHECK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230001U) == UMICOM_INTERRUPT_INVALID_ARGUMENT); return;
    } else { CHECK(0); }
    const UmicomU64 originalStatus = umicomInterruptModel.state.status;
    CHECK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230000U) == UMICOM_INTERRUPT_UNSAFE_CONTEXT);
    CHECK(umicomInterruptModel.state.status == originalStatus);
}

static void Run(const char *name)
{
    UmicomKernelCriticalSection section = 0U;
    UmicomKernelInterruptLease lease = 0U;
    if (strcmp(name, "initialise") == 0) { CHECK(Snapshot().depth == 0U); return; }
    if (strcmp(name, "reinitialise") == 0) {
        CHECK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230000U) == UMICOM_INTERRUPT_ALREADY_INITIALISED); return;
    }
    if (strcmp(name, "null-section") == 0) {
        CHECK(UmicomKernelCriticalSectionEnter(11U, NULL) == UMICOM_INTERRUPT_INVALID_ARGUMENT); return;
    }
    if (strcmp(name, "zero-owner") == 0) {
        CHECK(UmicomKernelCriticalSectionEnter(0U, &section) == UMICOM_INTERRUPT_INVALID_ARGUMENT); return;
    }
    if (strcmp(name, "disabled-restore") == 0 || strcmp(name, "enabled-restore") == 0 ||
        strcmp(name, "bit-only-restore") == 0 || strcmp(name, "nested-restore") == 0) {
        const UmicomU64 start = strcmp(name, "disabled-restore") == 0 ? 0U : 8U;
        umicomInterruptModel.state.status = start;
        section = Enter(11U);
        if (strcmp(name, "nested-restore") == 0) {
            const UmicomU64 inner = Enter(11U);
            CHECK(Snapshot().outerDeliveryEnabled == UMICOM_TRUE);
            Leave(inner); CHECK((umicomInterruptModel.state.status & 8U) == 0U);
        }
        /* A changed MPIE bit must survive; Leave must not restore all mstatus. */
        if (strcmp(name, "bit-only-restore") == 0) umicomInterruptModel.state.status |= 0x80U;
        Leave(section);
        CHECK((umicomInterruptModel.state.status & 8U) == start);
        if (strcmp(name, "bit-only-restore") == 0) CHECK((umicomInterruptModel.state.status & 0x80U) != 0U);
        return;
    }
    if (strcmp(name, "wrong-nested-owner") == 0) {
        section = Enter(11U); UmicomU64 out = 77U;
        CHECK(UmicomKernelCriticalSectionEnter(12U, &out) == UMICOM_INTERRUPT_WRONG_OWNER);
        CHECK(out == 77U && Snapshot().depth == 1U); Leave(section); return;
    }
    if (strcmp(name, "out-of-order") == 0) {
        umicomInterruptModel.state.status = 8U;
        section = Enter(11U); const UmicomU64 inner = Enter(11U);
        CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_OUT_OF_ORDER);
        CHECK(Snapshot().depth == 2U && (umicomInterruptModel.state.status & 8U) == 0U);
        Leave(inner); Leave(section); CHECK((umicomInterruptModel.state.status & 8U) != 0U); return;
    }
    if (strcmp(name, "stale-section") == 0) {
        section = Enter(11U); Leave(section); const UmicomU64 fresh = Enter(11U);
        CHECK(fresh != section && UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_INVALID_TOKEN);
        Leave(fresh); return;
    }
    if (strcmp(name, "wrong-leave-owner") == 0 || strcmp(name, "zero-token") == 0) {
        section = Enter(11U);
        CHECK(UmicomKernelCriticalSectionLeave(strcmp(name, "zero-token") == 0 ? 11U : 12U,
            strcmp(name, "zero-token") == 0 ? 0U : section) ==
            (strcmp(name, "zero-token") == 0 ? UMICOM_INTERRUPT_INVALID_TOKEN : UMICOM_INTERRUPT_WRONG_OWNER));
        CHECK(Snapshot().depth == 1U); Leave(section); return;
    }
    if (strcmp(name, "depth-limit") == 0) {
        UmicomU64 scopes[UMICOM_INTERRUPT_SECTION_LIMIT];
        for (UmicomSize index = 0U; index < UMICOM_INTERRUPT_SECTION_LIMIT; ++index) scopes[index] = Enter(11U);
        UmicomU64 output = 55U;
        CHECK(UmicomKernelCriticalSectionEnter(11U, &output) == UMICOM_INTERRUPT_DEPTH_LIMIT && output == 55U);
        for (UmicomSize count = UMICOM_INTERRUPT_SECTION_LIMIT; count != 0U; --count) Leave(scopes[count-1U]);
        CHECK(Snapshot().highestDepth == UMICOM_INTERRUPT_SECTION_LIMIT); return;
    }
    if (strncmp(name, "poison-", 7U) == 0) {
        section = Enter(11U);
        if (strcmp(name, "poison-mie") == 0) umicomInterruptModel.state.status |= 8U;
        else if (strcmp(name, "poison-source") == 0) umicomInterruptModel.state.sources |= 0x80U;
        else if (strcmp(name, "poison-vector") == 0) umicomInterruptModel.state.vector += 4U;
        else if (strcmp(name, "poison-scratch") == 0) umicomInterruptModel.state.scratch += 16U;
        else if (strcmp(name, "poison-translation") == 0) umicomInterruptModel.state.translation = 1U;
        else if (strcmp(name, "poison-delegation") == 0) umicomInterruptModel.state.delegation = 0x80U;
        else if (strcmp(name, "poison-hart") == 0) umicomInterruptModel.state.hart = 1U;
        else CHECK(0);
        CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_POISONED);
        UmicomKernelInterruptSnapshot poison;
        CHECK(UmicomKernelInterruptSnapshotRead(&poison) == UMICOM_INTERRUPT_POISONED);
        CHECK(poison.poisoned == UMICOM_TRUE && poison.depth == 1U && (umicomInterruptModel.state.status & 8U) == 0U);
        CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE);
        CHECK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230000U) == UMICOM_INTERRUPT_POISONED); return;
    }
    if (strcmp(name, "foreign-vector") == 0) {
        umicomInterruptModel.state.vector += 4U;
        CHECK(UmicomKernelCriticalSectionEnter(11U, &section) == UMICOM_INTERRUPT_UNSAFE_CONTEXT);
        umicomInterruptModel.state.vector -= 4U;
        section = Enter(11U); Leave(section); return;
    }
    if (strcmp(name, "scope-exhaustion") == 0) {
        UmicomKernelInterruptTestTokenLimit(); section = Enter(11U);
        CHECK(section == ~(UmicomU64)0U); Leave(section);
        CHECK(UmicomKernelCriticalSectionEnter(11U, &section) == UMICOM_INTERRUPT_TOKEN_EXHAUSTED);
        CHECK(Snapshot().depth == 0U); return;
    }
    if (strcmp(name, "source-needs-section") == 0) {
        CHECK(UmicomKernelInterruptSourceAcquire(11U, 0x80U, &lease) == UMICOM_INTERRUPT_SECTION_REQUIRED); return;
    }
    if (strcmp(name, "pending-acquire") == 0 || strcmp(name, "enabled-acquire") == 0 ||
        strcmp(name, "delegated-acquire") == 0 || strcmp(name, "future-acquire") == 0) {
        if (strcmp(name, "pending-acquire") == 0) umicomInterruptModel.state.pending = 0x80U;
        if (strcmp(name, "enabled-acquire") == 0) umicomInterruptModel.state.sources = 0x80U;
        if (strcmp(name, "delegated-acquire") == 0) umicomInterruptModel.state.delegation = 0x80U;
        if (strcmp(name, "future-acquire") == 0) umicomInterruptModel.compare = 1000000U;
        section = Enter(11U); lease = 55U;
        const UmicomKernelInterruptStatus expected = strcmp(name, "pending-acquire") == 0 ? UMICOM_INTERRUPT_SOURCE_PENDING :
            strcmp(name, "enabled-acquire") == 0 ? UMICOM_INTERRUPT_SOURCE_ENABLED :
            strcmp(name, "future-acquire") == 0 ? UMICOM_INTERRUPT_DEVICE_ACTIVE : UMICOM_INTERRUPT_SOURCE_BUSY;
        CHECK(UmicomKernelInterruptSourceAcquire(11U, 0x80U, &lease) == expected && lease == 55U);
        Leave(section); return;
    }
    if (strcmp(name, "unsupported-source") == 0 || strcmp(name, "null-lease") == 0) {
        section = Enter(11U);
        CHECK(UmicomKernelInterruptSourceAcquire(11U, strcmp(name, "null-lease") == 0 ? 0x80U : 0x800U,
            strcmp(name, "null-lease") == 0 ? NULL : &lease) ==
            (strcmp(name, "null-lease") == 0 ? UMICOM_INTERRUPT_INVALID_ARGUMENT : UMICOM_INTERRUPT_SOURCE_UNSUPPORTED));
        Leave(section); return;
    }
    if (strcmp(name, "switch-scope") == 0 || strcmp(name, "switch-restored") == 0) {
        section = Enter(11U); CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE);
        Leave(section); CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_TRUE); return;
    }
    if (strcmp(name, "switch-live-delivery") == 0 || strcmp(name, "switch-live-source") == 0) {
        if (strcmp(name, "switch-live-delivery") == 0) umicomInterruptModel.state.status = 8U;
        else umicomInterruptModel.state.sources = 0x80U;
        const UmicomU64 oldStatus = umicomInterruptModel.state.status;
        const UmicomU64 oldSources = umicomInterruptModel.state.sources;
        CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE);
        CHECK(umicomInterruptModel.state.status == oldStatus && umicomInterruptModel.state.sources == oldSources);
        return;
    }
    if (strcmp(name, "repeated-lifetimes") == 0) {
        UmicomU64 previous = 0U;
        for (UmicomSize iteration = 0U; iteration < 10000U; ++iteration) {
            section = Enter(11U); lease = Acquire(); CHECK(lease > previous); previous = lease;
            OK(UmicomKernelInterruptSourceEnable(11U, lease)); OK(UmicomKernelInterruptSourceDisable(11U, lease));
            OK(UmicomKernelInterruptSourceRelease(11U, lease)); Leave(section);
        }
        CHECK(Snapshot().depth == 0U && Snapshot().leasedSources == 0U); return;
    }
    if (strcmp(name, "modelled-nesting") == 0) {
        UmicomU64 scopes[UMICOM_INTERRUPT_SECTION_LIMIT]; UmicomSize depth = 0U;
        UmicomU64 seed = 0x9128834U;
        umicomInterruptModel.state.status = 8U;
        for (UmicomSize iteration = 0U; iteration < 10000U; ++iteration) {
            seed = seed * 6364136223846793005ULL + 1U;
            if (depth == 0U || (depth < UMICOM_INTERRUPT_SECTION_LIMIT && (seed >> 63U) != 0U)) {
                scopes[depth] = Enter(11U); ++depth;
            } else { Leave(scopes[depth-1U]); --depth; }
            CHECK(Snapshot().depth == depth);
            CHECK(((umicomInterruptModel.state.status & 8U) != 0U) == (depth == 0U));
        }
        while (depth != 0U) { Leave(scopes[depth-1U]); --depth; }
        return;
    }

    /* The remaining scenarios start with a valid leased, disabled timer. */
    section = Enter(11U); lease = Acquire();
    if (strcmp(name, "exclusive-source") == 0) {
        UmicomU64 duplicate = 81U;
        CHECK(UmicomKernelInterruptSourceAcquire(11U, 0x80U, &duplicate) == UMICOM_INTERRUPT_SOURCE_BUSY && duplicate == 81U);
    } else if (strcmp(name, "wrong-source-owner") == 0) {
        CHECK(UmicomKernelInterruptSourceEnable(12U, lease) == UMICOM_INTERRUPT_WRONG_OWNER);
    } else if (strcmp(name, "wrong-lease") == 0) {
        CHECK(UmicomKernelInterruptSourceEnable(11U, lease + 1U) == UMICOM_INTERRUPT_INVALID_TOKEN);
    } else if (strcmp(name, "enabled-release") == 0) {
        OK(UmicomKernelInterruptSourceEnable(11U, lease));
        CHECK(UmicomKernelInterruptSourceRelease(11U, lease) == UMICOM_INTERRUPT_SOURCE_ENABLED);
        OK(UmicomKernelInterruptSourceDisable(11U, lease));
    } else if (strcmp(name, "pending-release") == 0) {
        umicomInterruptModel.state.pending = 0x80U;
        CHECK(UmicomKernelInterruptSourceRelease(11U, lease) == UMICOM_INTERRUPT_SOURCE_PENDING);
        umicomInterruptModel.state.pending = 0U;
    } else if (strcmp(name, "future-release") == 0) {
        umicomInterruptModel.compare = 77U;
        CHECK(UmicomKernelInterruptSourceRelease(11U, lease) == UMICOM_INTERRUPT_DEVICE_ACTIVE);
        umicomInterruptModel.compare = ~(UmicomU64)0U;
    } else if (strcmp(name, "source-readback-enable") == 0 || strcmp(name, "source-readback-disable") == 0) {
        if (strcmp(name, "source-readback-disable") == 0) {
            OK(UmicomKernelInterruptSourceEnable(11U, lease)); umicomInterruptModel.refuseDisable = UMICOM_TRUE;
            CHECK(UmicomKernelInterruptSourceDisable(11U, lease) == UMICOM_INTERRUPT_HARDWARE_REFUSED);
        } else { umicomInterruptModel.refuseEnable = UMICOM_TRUE;
            CHECK(UmicomKernelInterruptSourceEnable(11U, lease) == UMICOM_INTERRUPT_HARDWARE_REFUSED); }
        CHECK((umicomInterruptModel.state.status & 8U) == 0U);
        CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_POISONED); return;
    } else if (strcmp(name, "delivery-inside-section") == 0) {
        CHECK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE) == UMICOM_INTERRUPT_SECTION_ACTIVE);
    } else if (strcmp(name, "source-specific-bits") == 0) {
        /* Set the unrelated bit before a new outer scope so it is inherited,
         * not an unauthorised mutation in the middle of an existing scope. */
        Leave(section); umicomInterruptModel.state.sources |= 0x800U; section = Enter(11U);
        OK(UmicomKernelInterruptSourceEnable(11U, lease)); CHECK(umicomInterruptModel.state.sources == 0x880U);
        OK(UmicomKernelInterruptSourceDisable(11U, lease)); CHECK(umicomInterruptModel.state.sources == 0x800U);
    } else if (strcmp(name, "stale-lease") == 0) {
        OK(UmicomKernelInterruptSourceRelease(11U, lease)); const UmicomU64 fresh = Acquire();
        CHECK(UmicomKernelInterruptSourceEnable(11U, lease) == UMICOM_INTERRUPT_INVALID_TOKEN); lease = fresh;
    } else if (strcmp(name, "switch-lease") == 0) {
        Leave(section); CHECK(UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE); section = Enter(11U);
    } else if (strncmp(name, "delivery-", 9U) == 0 || strcmp(name, "unmask-publication") == 0) {
        if (strcmp(name, "delivery-no-source") != 0) OK(UmicomKernelInterruptSourceEnable(11U, lease));
        Leave(section);
        if (strcmp(name, "delivery-owner") == 0) {
            CHECK(UmicomKernelInterruptDeliverySet(12U, UMICOM_TRUE) == UMICOM_INTERRUPT_WRONG_OWNER);
        } else if (strcmp(name, "delivery-no-source") == 0) {
            CHECK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE) == UMICOM_INTERRUPT_SOURCE_DISABLED);
        } else if (strcmp(name, "delivery-boolean") == 0) {
            CHECK(UmicomKernelInterruptDeliverySet(11U, (UmicomBoolean)2) == UMICOM_INTERRUPT_INVALID_ARGUMENT);
        } else if (strcmp(name, "delivery-unowned-source") == 0) {
            umicomInterruptModel.state.sources |= 0x800U;
            CHECK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE) == UMICOM_INTERRUPT_SOURCE_BUSY);
            umicomInterruptModel.state.sources &= ~(UmicomU64)0x800U;
        } else if (strcmp(name, "delivery-delegated") == 0) {
            umicomInterruptModel.state.delegation = 0x80U;
            CHECK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE) == UMICOM_INTERRUPT_SOURCE_BUSY);
            umicomInterruptModel.state.delegation = 0U;
        } else if (strcmp(name, "delivery-enable-disable") == 0) {
            OK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE)); CHECK((umicomInterruptModel.state.status & 8U) != 0U);
            OK(UmicomKernelInterruptDeliverySet(11U, UMICOM_FALSE)); CHECK((umicomInterruptModel.state.status & 8U) == 0U);
        } else if (strcmp(name, "unmask-publication") == 0) {
            OK(UmicomKernelInterruptDeliverySet(11U, UMICOM_TRUE)); section = Enter(11U);
            const UmicomU64 inner = Enter(11U); umicomInterruptModel.observeUnmask = UMICOM_TRUE;
            umicomInterruptModel.state.pending = 0x80U;
            Leave(inner); CHECK(umicomInterruptModel.deliveries == 0U);
            Leave(section); CHECK(umicomInterruptModel.deliveries == 1U && umicomInterruptModel.observedDepth == 0U);
        } else CHECK(0);
        section = Enter(11U);
        OK(UmicomKernelInterruptSourceDisable(11U, lease));
    } else CHECK(0);
    OK(UmicomKernelInterruptSourceRelease(11U, lease));
    Leave(section);
    CHECK(Snapshot().depth == 0U && Snapshot().leasedSources == 0U);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    UmicomInterruptModelPrepare();
    const char *const name = argv[1];
    if (strncmp(name, "initialise-", 11U) == 0 || strcmp(name, "uninitialised") == 0 ||
        strcmp(name, "switch-before-initialise") == 0) Early(name);
    else { Initialise(); Run(name); }
    printf("interrupt-policy.%s=pass\n", name);
    return 0;
}
