/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/process_registry/process_registry_tests.c
 *
 * PURPOSE:
 *   Exercise the actual registry, ELF loader, page mapper and physical allocator
 *   using aligned host storage. Test rights and lifetime failures without hiding
 *   their consequences behind mocked allocation or fake page-table counters.
 *
 * EDUCATIONAL NOTE:
 *   Only ProcessRun is substituted: a host CPU cannot enter RISC-V user mode.
 *   The substitute checks routing and recursive-call rejection, not privilege
 *   transitions. The guest acceptance test uses the real ProcessRun instead.
 *   Direct private-field changes below are labelled fault injection. They test
 *   defensive boundaries; they are not examples of using the public interface.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/process_registry.h"

alignas(4096) static UmicomU8 umicomTestRam[4096U * 256U];
static UmicomU8 umicomTestImage[8192U];
static UmicomKernelProcessRegistry umicomTestRegistry;
static UmicomKernelProcessRegistry umicomTestCopy;
static UmicomKernelProcessHandle umicomTestHandle;
static UmicomU64 umicomTestRuns;
static UmicomU64 umicomTestLastArgument;
static UmicomU64 umicomTestLastDeadline;
static unsigned umicomTestRunMode;
static unsigned umicomTestChecks;
static const UmicomU8 *umicomTestImageBytes = umicomTestImage;
static UmicomSize umicomTestImageSize = sizeof(umicomTestImage);

static void UmicomTestRequire(int condition, const char *reason)
{
    ++umicomTestChecks;
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", reason);
        exit(EXIT_FAILURE);
    }
}
static void UmicomTestStatus(UmicomKernelRegistryStatus actual,
    UmicomKernelRegistryStatus expected, const char *reason)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %s, got %s\n", reason,
            UmicomKernelRegistryStatusName(expected), UmicomKernelRegistryStatusName(actual));
    }
    UmicomTestRequire(actual == expected, reason);
}
static void UmicomTestPut(UmicomSize offset, UmicomU64 value, UmicomSize bytes)
{
    /* Encode file fields independently of the production parser. */
    for (UmicomSize index = 0U; index < bytes; ++index) {
        umicomTestImage[offset + index] = (UmicomU8)(value >> (UmicomU32)(index * 8U));
    }
}
static void UmicomTestPrepare(void)
{
    memset(umicomTestRam, 0xa5, sizeof(umicomTestRam));
    UmicomTestRequire(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomTestRam,
        sizeof(umicomTestRam)) == UMICOM_KERNEL_MEMORY_OK, "initialise real allocator over host storage");
    umicomTestImage[0] = 0x7fU; umicomTestImage[1] = 'E';
    umicomTestImage[2] = 'L'; umicomTestImage[3] = 'F';
    umicomTestImage[4] = 2U; umicomTestImage[5] = 1U; umicomTestImage[6] = 1U;
    UmicomTestPut(16U, 2U, 2U); UmicomTestPut(18U, 243U, 2U);
    UmicomTestPut(20U, 1U, 4U); UmicomTestPut(24U, 0x400000U, 8U);
    UmicomTestPut(32U, 64U, 8U); UmicomTestPut(48U, 1U, 4U);
    UmicomTestPut(52U, 64U, 2U); UmicomTestPut(54U, 56U, 2U); UmicomTestPut(56U, 1U, 2U);
    UmicomTestPut(64U, 1U, 4U); UmicomTestPut(68U, 5U, 4U);
    UmicomTestPut(72U, 4096U, 8U); UmicomTestPut(80U, 0x400000U, 8U);
    UmicomTestPut(96U, 16U, 8U); UmicomTestPut(104U, 16U, 8U); UmicomTestPut(112U, 4096U, 8U);
    umicomTestImage[4096U] = 0x13U; /* Inert RV64 instruction bytes; not executed here. */
    UmicomTestStatus(UmicomKernelProcessRegistryInitialize(&umicomTestRegistry),
        UMICOM_REGISTRY_OK, "initialise registry");
}
static UmicomKernelProcessHandle UmicomTestCreate(UmicomU64 owner, UmicomKernelProcessRights rights)
{
    UmicomKernelProcessHandle result = 0U;
    UmicomTestStatus(UmicomKernelProcessRegistryCreate(&umicomTestRegistry, owner,
        umicomTestImageBytes, umicomTestImageSize, rights, &result), UMICOM_REGISTRY_OK, "create loaded process");
    UmicomTestRequire(result != 0U, "publish nonzero handle");
    return result;
}
static UmicomKernelProcessInfo UmicomTestQuery(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomKernelProcessInfo info;
    UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, owner, handle, &info),
        UMICOM_REGISTRY_OK, "query owned process");
    return info;
}
static UmicomKernelRegistrySnapshot UmicomTestSnapshot(void)
{
    UmicomKernelRegistrySnapshot snapshot;
    UmicomTestStatus(UmicomKernelProcessRegistrySnapshotRead(&umicomTestRegistry, &snapshot),
        UMICOM_REGISTRY_OK, "snapshot and independent registry recount");
    return snapshot;
}
static UmicomKernelPhysicalMemorySnapshot UmicomTestPhysical(void)
{
    UmicomKernelPhysicalMemorySnapshot snapshot;
    UmicomTestRequire(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK,
        "read physical ownership counters");
    return snapshot;
}
static void UmicomTestClose(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomTestStatus(UmicomKernelProcessRegistryClose(&umicomTestRegistry, owner, handle),
        UMICOM_REGISTRY_OK, "close owned reference");
}
static void UmicomTestNoLeaks(void)
{
    /* Public owner cleanup is used for ordinary test teardown. A failed close
     * is a test failure; it is never erased with memset or allocator reset. */
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++index) {
        if (umicomTestRegistry.handles[index].occupied != UMICOM_FALSE) {
            const UmicomU64 owner = umicomTestRegistry.handles[index].owner;
            UmicomSize closed = 0U;
            UmicomTestStatus(UmicomKernelProcessRegistryCloseOwner(&umicomTestRegistry, owner, &closed),
                UMICOM_REGISTRY_OK, "teardown owner references");
            UmicomTestRequire(closed != 0U, "owner cleanup made progress");
        }
    }
    UmicomSize reaped = 0U;
    UmicomTestStatus(UmicomKernelProcessRegistryReap(&umicomTestRegistry, &reaped),
        UMICOM_REGISTRY_OK, "reap retained test ownership");
    const UmicomKernelRegistrySnapshot registry = UmicomTestSnapshot();
    const UmicomKernelPhysicalMemorySnapshot memory = UmicomTestPhysical();
    UmicomTestRequire(registry.objects == 0U && registry.handles == 0U &&
        registry.retainedWithoutHandles == 0U && memory.allocatedFrames == 0U &&
        memory.reservedFrames == 0U && memory.freeFrames == memory.totalFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "no lost reference or physical frame");
}

/* Native substitute for the architecture boundary only. The production registry
 * calls this same symbol; no rights, lifetime or loading logic is replaced. */
UmicomKernelProcessStatus UmicomKernelProcessRun(
    UmicomKernelProcess *process, UmicomU64 argument, UmicomU64 deadlineTicks)
{
    if (deadlineTicks == 0U || deadlineTicks > 10000000U) return UMICOM_PROCESS_INVALID_ARGUMENT;
    if (process->state != UMICOM_PROCESS_READY) return UMICOM_PROCESS_BAD_STATE;
    ++umicomTestRuns;
    umicomTestLastArgument = argument;
    umicomTestLastDeadline = deadlineTicks;
    if (umicomTestRunMode == 1U) return UMICOM_PROCESS_ENTRY_REFUSED;
    if (umicomTestRunMode == 2U) {
        process->state = UMICOM_PROCESS_MONITOR_ERROR;
        process->quiesced = UMICOM_FALSE;
        return UMICOM_PROCESS_MACHINE_STATE_ERROR;
    }
    if (umicomTestRunMode == 3U) {
        UmicomKernelProcessInfo info;
        UmicomKernelProcessHandle duplicate = 0U;
        UmicomSize count = 0U;
        UmicomTestStatus(UmicomKernelProcessRegistryClose(&umicomTestRegistry, 11U, umicomTestHandle),
            UMICOM_REGISTRY_BUSY, "recursive close refused during run");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, umicomTestHandle, &info),
            UMICOM_REGISTRY_BUSY, "recursive query refused during run");
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U,
            umicomTestHandle, 1U, &duplicate), UMICOM_REGISTRY_BUSY, "recursive duplicate refused");
        UmicomTestStatus(UmicomKernelProcessRegistryCloseOwner(&umicomTestRegistry, 11U, &count),
            UMICOM_REGISTRY_BUSY, "recursive owner cleanup refused");
    }
    process->state = UMICOM_PROCESS_EXITED;
    process->quiesced = UMICOM_TRUE;
    process->report.exitValue = process->identity + argument + 33U;
    process->report.trapCause = 8U;
    process->report.callCount = 3U;
    return UMICOM_PROCESS_OK;
}

static void UmicomTestBasic(const char *name)
{
    UmicomKernelProcessInfo info;
    UmicomKernelProcessHandle output = 0xfeedU;
    if (strcmp(name, "null-registry") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryInitialize((UmicomKernelProcessRegistry *)0),
            UMICOM_REGISTRY_INVALID_ARGUMENT, "null initialise");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery((UmicomKernelProcessRegistry *)0,
            11U, 1U, &info), UMICOM_REGISTRY_INVALID_ARGUMENT, "null query");
        return;
    }
    if (strcmp(name, "uninitialised") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryValidate(&umicomTestCopy),
            UMICOM_REGISTRY_NOT_INITIALISED, "uninitialised domain");
        return;
    }
    if (strcmp(name, "copied-registry") == 0) {
        memcpy(&umicomTestCopy, &umicomTestRegistry, sizeof(umicomTestCopy));
        UmicomTestStatus(UmicomKernelProcessRegistryValidate(&umicomTestCopy),
            UMICOM_REGISTRY_NOT_INITIALISED, "copied domain cannot own interior pointers");
        return;
    }
    if (strcmp(name, "reinitialise") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryInitialize(&umicomTestRegistry),
            UMICOM_REGISTRY_BAD_STATE, "generations cannot reset");
        return;
    }
    if (strcmp(name, "dirty-initial-storage") == 0) {
        umicomTestCopy.handles[3].owner = 9U;
        UmicomTestStatus(UmicomKernelProcessRegistryInitialize(&umicomTestCopy),
            UMICOM_REGISTRY_BAD_STATE, "nonempty storage is not discarded");
        return;
    }
    if (strcmp(name, "bad-owner") == 0 || strcmp(name, "bad-rights") == 0 ||
        strcmp(name, "null-output") == 0 || strcmp(name, "empty-image") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryCreate(&umicomTestRegistry,
            strcmp(name, "bad-owner") == 0 ? 0U : 11U, umicomTestImageBytes,
            strcmp(name, "empty-image") == 0 ? 0U : umicomTestImageSize,
            strcmp(name, "bad-rights") == 0 ? 0x80000000U : UMICOM_PROCESS_RIGHT_ALL,
            strcmp(name, "null-output") == 0 ? (UmicomKernelProcessHandle *)0 : &output),
            UMICOM_REGISTRY_INVALID_ARGUMENT, "invalid admission");
        UmicomTestRequire(output == 0xfeedU && UmicomTestSnapshot().objects == 0U,
            "invalid admission publishes nothing");
        return;
    }
    if (strcmp(name, "malformed-image") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryCreate(&umicomTestRegistry, 11U,
            umicomTestImageBytes, 63U, UMICOM_PROCESS_RIGHT_ALL, &output),
            UMICOM_REGISTRY_LOAD_FAILED, "truncated ELF rejected");
        UmicomTestRequire(output == 0xfeedU && UmicomTestPhysical().allocatedFrames == 0U,
            "failed admission is allocation-neutral");
        return;
    }
    umicomTestHandle = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
    info = UmicomTestQuery(11U, umicomTestHandle);
    UmicomTestRequire(info.identity == 1U && info.state == UMICOM_PROCESS_READY &&
        info.references == 1U && info.backingPages != 0U, "ready process metadata");
    if (strcmp(name, "create-query") == 0 || strcmp(name, "real-linked-elf") == 0) return;
    if (strcmp(name, "zero-handle") == 0 || strcmp(name, "bad-index") == 0 || strcmp(name, "bad-generation") == 0) {
        UmicomKernelProcessHandle invalid = 0U;
        if (strcmp(name, "bad-index") == 0) invalid = ((UmicomU64)1U << 32U) | 33U;
        if (strcmp(name, "bad-generation") == 0) invalid = umicomTestHandle + ((UmicomU64)1U << 32U);
        info.identity = 0xfeedU;
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, invalid, &info),
            UMICOM_REGISTRY_INVALID_HANDLE, "invalid token refused");
        UmicomTestRequire(info.identity == 0xfeedU, "failure leaves snapshot untouched");
        return;
    }
    if (strcmp(name, "wrong-owner") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 12U, umicomTestHandle, &info),
            UMICOM_REGISTRY_WRONG_OWNER, "foreign token conveys no authority");
        UmicomTestStatus(UmicomKernelProcessRegistryClose(&umicomTestRegistry, 12U, umicomTestHandle),
            UMICOM_REGISTRY_WRONG_OWNER, "foreign close refused");
        return;
    }
    if (strcmp(name, "stale-after-close") == 0 || strcmp(name, "slot-reuse") == 0) {
        UmicomTestClose(11U, umicomTestHandle);
        const UmicomKernelProcessHandle replacement = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
        UmicomTestRequire(replacement != umicomTestHandle &&
            (UmicomU32)replacement == (UmicomU32)umicomTestHandle, "same slot has a new generation");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, umicomTestHandle, &info),
            UMICOM_REGISTRY_INVALID_HANDLE, "stale token never selects replacement");
        UmicomTestRequire(UmicomTestQuery(11U, replacement).identity == 2U, "process identity also advances");
        return;
    }
    if (strcmp(name, "final-close") == 0 || strcmp(name, "scrub-on-release") == 0) {
        /* Test-only inspection records a backing address before its owner dies. */
        const UmicomAddress frame = umicomTestRegistry.objects[0].process.pages[0].physicalBase;
        UmicomTestClose(11U, umicomTestHandle);
        UmicomTestRequire(UmicomTestPhysical().allocatedFrames == 0U, "final reference releases all frames");
        for (UmicomSize index = 0U; index < 4096U; ++index) {
            UmicomTestRequire(((const UmicomU8 *)frame)[index] == 0U, "released data page scrubbed");
        }
        return;
    }
    UmicomTestRequire(0, "unknown basic test name");
}

static void UmicomTestRights(const char *name)
{
    umicomTestHandle = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
    UmicomKernelProcessHandle duplicate = 0U;
    UmicomKernelProcessInfo info;
    if (strcmp(name, "query-only") == 0 || strcmp(name, "duplicate-subset") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_QUERY, &duplicate), UMICOM_REGISTRY_OK, "duplicate with less authority");
        info = UmicomTestQuery(11U, duplicate);
        UmicomTestRequire(info.rights == UMICOM_PROCESS_RIGHT_QUERY && info.references == 2U,
            "independent restricted reference");
        UmicomTestStatus(UmicomKernelProcessRegistryRun(&umicomTestRegistry, 11U, duplicate, 7U, 1000U),
            UMICOM_REGISTRY_ACCESS_DENIED, "query reference cannot run");
        UmicomTestRequire(umicomTestRuns == 0U, "denied run never reaches execution adapter");
    } else if (strcmp(name, "duplicate-requires-right") == 0 || strcmp(name, "grant-requires-right") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_QUERY), UMICOM_REGISTRY_OK, "reduce to query");
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_QUERY, &duplicate), UMICOM_REGISTRY_ACCESS_DENIED, "cannot duplicate without permission");
        UmicomTestStatus(UmicomKernelProcessRegistryGrant(&umicomTestRegistry, 11U, umicomTestHandle, 12U,
            UMICOM_PROCESS_RIGHT_QUERY, &duplicate), UMICOM_REGISTRY_ACCESS_DENIED, "cannot grant without permission");
    } else if (strcmp(name, "no-escalation") == 0 || strcmp(name, "restrict-irreversible") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_DUPLICATE), UMICOM_REGISTRY_OK, "retain only query and duplicate");
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_RUN, &duplicate), UMICOM_REGISTRY_ACCESS_DENIED, "duplicate cannot amplify rights");
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_ALL), UMICOM_REGISTRY_ACCESS_DENIED, "restriction cannot be reversed");
    } else if (strcmp(name, "grant-requires-transfer") == 0 ||
        strcmp(name, "grant-requires-duplicate") == 0 || strcmp(name, "grant-no-escalation") == 0) {
        const UmicomKernelProcessRights limited = strcmp(name, "grant-requires-transfer") == 0
            ? UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_DUPLICATE
            : strcmp(name, "grant-requires-duplicate") == 0
            ? UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_TRANSFER
            : UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_DUPLICATE | UMICOM_PROCESS_RIGHT_TRANSFER;
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U,
            umicomTestHandle, limited), UMICOM_REGISTRY_OK, "restrict source grant authority");
        duplicate = 0xfeedU;
        UmicomTestStatus(UmicomKernelProcessRegistryGrant(&umicomTestRegistry, 11U,
            umicomTestHandle, 12U, strcmp(name, "grant-no-escalation") == 0
                ? UMICOM_PROCESS_RIGHT_RUN : UMICOM_PROCESS_RIGHT_QUERY, &duplicate),
            UMICOM_REGISTRY_ACCESS_DENIED, "grant needs both authority and a rights subset");
        UmicomTestRequire(duplicate == 0xfeedU && UmicomTestSnapshot().handles == 1U,
            "refused grant does not publish authority");
    } else if (strcmp(name, "zero-rights-close") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U, umicomTestHandle, 0U),
            UMICOM_REGISTRY_OK, "drop every use right");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, umicomTestHandle, &info),
            UMICOM_REGISTRY_ACCESS_DENIED, "zero rights cannot query");
        UmicomTestClose(11U, umicomTestHandle);
    } else if (strcmp(name, "grant-owner-binding") == 0 || strcmp(name, "owner-cleanup-shared") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryGrant(&umicomTestRegistry, 11U, umicomTestHandle, 12U,
            UMICOM_PROCESS_RIGHT_QUERY, &duplicate), UMICOM_REGISTRY_OK, "explicit grant to another owner");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, duplicate, &info),
            UMICOM_REGISTRY_WRONG_OWNER, "sender cannot use receiver token");
        UmicomSize closed = 0U;
        UmicomTestStatus(UmicomKernelProcessRegistryCloseOwner(&umicomTestRegistry, 11U, &closed),
            UMICOM_REGISTRY_OK, "release sender references");
        UmicomTestRequire(closed == 1U && UmicomTestQuery(12U, duplicate).references == 1U &&
            UmicomTestPhysical().allocatedFrames != 0U, "receiver keeps process alive");
    } else if (strcmp(name, "duplicate-no-allocation") == 0) {
        const UmicomKernelPhysicalMemorySnapshot before = UmicomTestPhysical();
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U, umicomTestHandle,
            UMICOM_PROCESS_RIGHT_ALL, &duplicate), UMICOM_REGISTRY_OK, "alias existing process");
        const UmicomKernelPhysicalMemorySnapshot after = UmicomTestPhysical();
        UmicomTestRequire(before.allocatedFrames == after.allocatedFrames, "duplicate does not clone memory");
        UmicomTestClose(11U, umicomTestHandle);
        UmicomTestRequire(UmicomTestQuery(11U, duplicate).references == 1U, "remaining duplicate stays valid");
    } else if (strcmp(name, "unknown-rights") == 0) {
        UmicomTestStatus(UmicomKernelProcessRegistryRestrict(&umicomTestRegistry, 11U, umicomTestHandle, 0x10U),
            UMICOM_REGISTRY_INVALID_ARGUMENT, "unknown rights refused");
        UmicomTestRequire(UmicomTestQuery(11U, umicomTestHandle).rights == UMICOM_PROCESS_RIGHT_ALL,
            "failed restriction unchanged");
    } else UmicomTestRequire(0, "unknown rights case");
}

static void UmicomTestLimits(const char *name)
{
    UmicomKernelProcessHandle output = 0xfeedU;
    if (strcmp(name, "object-capacity") == 0) {
        for (UmicomU64 owner = 1U; owner <= UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT; ++owner) {
            (void)UmicomTestCreate(owner, UMICOM_PROCESS_RIGHT_ALL);
        }
        UmicomTestStatus(UmicomKernelProcessRegistryCreate(&umicomTestRegistry, 99U,
            umicomTestImageBytes, umicomTestImageSize, UMICOM_PROCESS_RIGHT_ALL, &output),
            UMICOM_REGISTRY_PROCESS_LIMIT, "process table refuses exhaustion");
        UmicomTestRequire(output == 0xfeedU, "capacity refusal publishes no handle");
        return;
    }
    if (strcmp(name, "identity-exhaustion") == 0) {
        /* Test-only fault boundary: reach the final identity without billions
         * of admissions. Production code never rewinds this counter. */
        umicomTestRegistry.nextIdentity = ~(UmicomU64)0U;
        const UmicomKernelProcessHandle last = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
        UmicomTestRequire(UmicomTestQuery(11U, last).identity == ~(UmicomU64)0U, "last identity is representable");
        UmicomTestStatus(UmicomKernelProcessRegistryCreate(&umicomTestRegistry, 11U,
            umicomTestImageBytes, umicomTestImageSize, UMICOM_PROCESS_RIGHT_ALL, &output),
            UMICOM_REGISTRY_IDENTITY_EXHAUSTED, "identity does not wrap");
        return;
    }
    umicomTestHandle = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
    if (strcmp(name, "generation-retirement") == 0) {
        /* Inject the last generation, then use the normal Close implementation. */
        umicomTestRegistry.handles[0].generation = 0xffffffffU;
        const UmicomKernelProcessHandle last = ((UmicomU64)0xffffffffU << 32U) | 1U;
        UmicomTestClose(11U, last);
        output = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
        UmicomTestRequire((UmicomU32)output == 2U && UmicomTestSnapshot().retiredHandles == 1U,
            "retired slot is not recycled");
        return;
    }
    if (strcmp(name, "owner-capacity") == 0) {
        for (UmicomSize count = 1U; count < UMICOM_PROCESS_REGISTRY_OWNER_LIMIT; ++count) {
            UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U,
                umicomTestHandle, UMICOM_PROCESS_RIGHT_QUERY, &output), UMICOM_REGISTRY_OK, "fill owner quota");
        }
        output = 0xfeedU;
        UmicomTestStatus(UmicomKernelProcessRegistryDuplicate(&umicomTestRegistry, 11U,
            umicomTestHandle, 1U, &output), UMICOM_REGISTRY_OWNER_LIMIT, "owner cannot monopolise handles");
        UmicomTestRequire(output == 0xfeedU, "quota failure leaves output intact");
        return;
    }
    UmicomTestRequire(strcmp(name, "handle-capacity") == 0, "known limits case");
    for (UmicomU64 owner = 11U; owner < 15U; ++owner) {
        const UmicomSize already = owner == 11U ? 1U : 0U;
        for (UmicomSize count = already; count < UMICOM_PROCESS_REGISTRY_OWNER_LIMIT; ++count) {
            UmicomTestStatus(UmicomKernelProcessRegistryGrant(&umicomTestRegistry, 11U,
                umicomTestHandle, owner, UMICOM_PROCESS_RIGHT_QUERY, &output),
                UMICOM_REGISTRY_OK, "fill global handle capacity");
        }
    }
    UmicomTestStatus(UmicomKernelProcessRegistryGrant(&umicomTestRegistry, 11U,
        umicomTestHandle, 20U, 1U, &output), UMICOM_REGISTRY_HANDLE_LIMIT, "handle table cannot overrun");
    UmicomTestRequire(UmicomTestSnapshot().handles == UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT,
        "exact capacity reached without overflow");
}

static void UmicomTestRunBoundary(const char *name)
{
    umicomTestHandle = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
    if (strcmp(name, "run-refused") == 0) umicomTestRunMode = 1U;
    else if (strcmp(name, "unsafe-run-return") == 0) umicomTestRunMode = 2U;
    else if (strcmp(name, "recursive-run-guard") == 0) umicomTestRunMode = 3U;
    const UmicomKernelRegistryStatus expected = umicomTestRunMode == 1U || umicomTestRunMode == 2U
        ? UMICOM_REGISTRY_RUN_FAILED : UMICOM_REGISTRY_OK;
    UmicomTestStatus(UmicomKernelProcessRegistryRun(&umicomTestRegistry, 11U,
        umicomTestHandle, 19U, 1000U), expected, "run status is not hidden");
    UmicomTestRequire(umicomTestRuns == 1U && umicomTestLastArgument == 19U &&
        umicomTestLastDeadline == 1000U, "route exact execution arguments");
    const UmicomKernelProcessInfo info = UmicomTestQuery(11U, umicomTestHandle);
    if (umicomTestRunMode == 1U) {
        UmicomTestRequire(info.state == UMICOM_PROCESS_READY, "entry refusal retained ready owner");
    } else if (umicomTestRunMode == 2U) {
        UmicomTestStatus(UmicomKernelProcessRegistryClose(&umicomTestRegistry, 11U, umicomTestHandle),
            UMICOM_REGISTRY_BAD_STATE, "unsafe return cannot be reclaimed");
        /* Test-only recovery: no RISC-V execution occurred in the native stub.
         * Actual unsafe machine returns must not be repaired by guessing. */
        umicomTestRegistry.objects[0].process.quiesced = UMICOM_TRUE;
    } else {
        UmicomTestRequire(info.state == UMICOM_PROCESS_EXITED && info.exitValue == 53U,
            "retain terminal report after exit");
        UmicomTestStatus(UmicomKernelProcessRegistryRun(&umicomTestRegistry, 11U,
            umicomTestHandle, 19U, 1000U), UMICOM_REGISTRY_RUN_FAILED, "terminal image not restarted");
    }
}

static void UmicomTestRecovery(const char *name)
{
    umicomTestHandle = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
    UmicomKernelProcess *const process = &umicomTestRegistry.objects[0].process;
    if (strcmp(name, "cleanup-retry") == 0 || strcmp(name, "owner-cleanup-partial") == 0) {
        UmicomKernelProcess *damaged = process;
        UmicomKernelProcessHandle retained = umicomTestHandle;
        if (strcmp(name, "owner-cleanup-partial") == 0) {
            retained = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
            damaged = &umicomTestRegistry.objects[1].process;
        }
        /* Fault injection: make the real page-table validator reject teardown.
         * No production hook or fake free result is used. The correct count is
         * restored only after we verify that the handle survived the refusal. */
        const UmicomSize mappings = damaged->space.mappedPages;
        ++damaged->space.mappedPages;
        if (strcmp(name, "owner-cleanup-partial") == 0) {
            UmicomSize closed = 99U;
            UmicomTestStatus(UmicomKernelProcessRegistryCloseOwner(&umicomTestRegistry, 11U, &closed),
                UMICOM_REGISTRY_CLEANUP_FAILED, "owner cleanup reports failed final release");
            UmicomTestRequire(closed == 1U, "only the earlier successful close was counted");
        } else {
            UmicomTestStatus(UmicomKernelProcessRegistryClose(&umicomTestRegistry, 11U, retained),
                UMICOM_REGISTRY_CLEANUP_FAILED, "last close retains failed teardown");
        }
        UmicomTestRequire(UmicomTestQuery(11U, retained).state == UMICOM_PROCESS_CLEANUP_REQUIRED &&
            UmicomTestSnapshot().handles == 1U, "handle survives cleanup failure");
        damaged->space.mappedPages = mappings; /* Repair only the injected inconsistency. */
        UmicomTestClose(11U, retained);
    } else if (strcmp(name, "retained-reap") == 0) {
        /* Inject the documented post-rollback quarantine shape. Loader rollback
         * itself is exercised separately by the allocation-failure sweep. */
        umicomTestRegistry.handles[0].occupied = UMICOM_FALSE;
        umicomTestRegistry.handles[0].owner = 0U;
        umicomTestRegistry.handles[0].rights = 0U;
        ++umicomTestRegistry.handles[0].generation;
        umicomTestRegistry.objects[0].references = 0U;
        process->state = UMICOM_PROCESS_CLEANUP_REQUIRED;
        UmicomTestRequire(UmicomTestSnapshot().retainedWithoutHandles == 1U, "quarantine remains visible");
        UmicomSize reaped = 0U;
        UmicomTestStatus(UmicomKernelProcessRegistryReap(&umicomTestRegistry, &reaped),
            UMICOM_REGISTRY_OK, "maintenance releases retained owner");
        UmicomTestRequire(reaped == 1U, "one retained object reaped");
    } else {
        const UmicomSize saved = umicomTestRegistry.objects[0].references;
        umicomTestRegistry.objects[0].references = saved + 1U;
        UmicomTestStatus(UmicomKernelProcessRegistryValidate(&umicomTestRegistry),
            UMICOM_REGISTRY_CORRUPT_STATE, "independent count catches reference drift");
        umicomTestRegistry.objects[0].references = saved;
    }
}

static void UmicomTestAllocationSweep(void)
{
    /* Reserve the RAM tail to leave each small budget without reinitialising
     * the allocator under live ownership. Both successes and failures must
     * return to zero allocations once their references have been released. */
    for (UmicomSize budget = 0U; budget <= 20U; ++budget) {
        const UmicomAddress tail = (UmicomAddress)umicomTestRam + budget * 4096U;
        const UmicomSize bytes = sizeof(umicomTestRam) - budget * 4096U;
        UmicomTestRequire(UmicomKernelPhysicalMemoryReserveRange(tail, bytes) == UMICOM_KERNEL_MEMORY_OK,
            "constrain allocation budget");
        UmicomKernelProcessHandle handle = 0xfeedU;
        const UmicomKernelRegistryStatus status = UmicomKernelProcessRegistryCreate(&umicomTestRegistry,
            11U, umicomTestImageBytes, umicomTestImageSize, UMICOM_PROCESS_RIGHT_ALL, &handle);
        if (status == UMICOM_REGISTRY_OK) UmicomTestClose(11U, handle);
        else {
            UmicomTestStatus(status, UMICOM_REGISTRY_LOAD_FAILED, "ordinary exhausted load rolls back");
            UmicomTestRequire(handle == 0xfeedU, "failed budget does not publish a handle");
        }
        UmicomTestRequire(UmicomTestSnapshot().objects == 0U &&
            UmicomTestPhysical().allocatedFrames == 0U, "budget case leaks no owner or frame");
        UmicomTestRequire(UmicomKernelPhysicalMemoryReleaseReservedRange(tail, bytes) == UMICOM_KERNEL_MEMORY_OK,
            "return budget reservation");
    }
}

static void UmicomTestRepeatedLifetimes(void)
{
    UmicomKernelProcessHandle previous = 0U;
    for (UmicomSize iteration = 0U; iteration < 2000U; ++iteration) {
        const UmicomKernelProcessHandle current = UmicomTestCreate(11U, UMICOM_PROCESS_RIGHT_ALL);
        UmicomKernelProcessInfo ignored;
        UmicomTestRequire(current != previous, "token never repeats across lifetimes");
        UmicomTestStatus(UmicomKernelProcessRegistryQuery(&umicomTestRegistry, 11U, previous, &ignored),
            UMICOM_REGISTRY_INVALID_HANDLE, "older token stays invalid after reuse");
        UmicomTestClose(11U, current);
        previous = current;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return EXIT_FAILURE;
    UmicomTestPrepare();
    const char *const name = argv[1];
    UmicomU8 *file = (UmicomU8 *)0;
    if (strcmp(name, "real-linked-elf") == 0) {
        if (argc != 3) return EXIT_FAILURE;
        FILE *const input = fopen(argv[2], "rb");
        UmicomTestRequire(input != NULL && fseek(input, 0L, SEEK_END) == 0, "open real separately linked ELF");
        const long count = ftell(input);
        UmicomTestRequire(count > 0L && (unsigned long)count <= UMICOM_EXECUTABLE_MAX_FILE_BYTES,
            "bounded real ELF size");
        rewind(input);
        file = malloc((size_t)count);
        UmicomTestRequire(file != NULL && fread(file, 1U, (size_t)count, input) == (size_t)count,
            "read actual ELF bytes");
        UmicomTestRequire(fclose(input) == 0, "close ELF input");
        umicomTestImageBytes = file;
        umicomTestImageSize = (UmicomSize)count;
    }
    if (strcmp(name, "allocation-failure-sweep") == 0) UmicomTestAllocationSweep();
    else if (strcmp(name, "repeated-lifetimes") == 0) UmicomTestRepeatedLifetimes();
    else if (strstr(" query-only duplicate-subset duplicate-requires-right grant-requires-right no-escalation restrict-irreversible zero-rights-close grant-owner-binding owner-cleanup-shared duplicate-no-allocation unknown-rights grant-requires-transfer grant-requires-duplicate grant-no-escalation ", name) != NULL) UmicomTestRights(name);
    else if (strstr(" object-capacity identity-exhaustion generation-retirement owner-capacity handle-capacity ", name) != NULL) UmicomTestLimits(name);
    else if (strstr(" run-route run-refused unsafe-run-return recursive-run-guard ", name) != NULL) UmicomTestRunBoundary(name);
    else if (strstr(" cleanup-retry owner-cleanup-partial retained-reap corrupt-reference-count ", name) != NULL) UmicomTestRecovery(name);
    else UmicomTestBasic(name);
    UmicomTestNoLeaks();
    free(file);
    printf("PASS %s (%u checks; native policy, not RISC-V execution)\n", name, umicomTestChecks);
    return EXIT_SUCCESS;
}
