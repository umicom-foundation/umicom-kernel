/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/process_registry_validation.c
 *
 * PURPOSE:
 *   Exercise process handles with the real independently linked ELF and the
 *   real user execution monitor. Keep all earlier acceptance paths unchanged.
 *
 * EDUCATIONAL OVERVIEW:
 *   A handle test is not just an integer lookup. It must prove that a denied
 *   operation never starts a program, that another owner cannot borrow a raw
 *   token, and that closing one alias does not release another owner's memory.
 *   This validation also runs a faulting and an endless program through the
 *   registry, then checks that their terminal images can be reclaimed safely.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process_registry.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

/* These symbols already carry the separate diagnostic ELF as read-only bytes.
 * We reuse that loader input; no second program copy or loader is introduced. */
extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
static UmicomKernelProcessRegistry umicomAcceptanceRegistry;
static UmicomSize umicomRegistryImageBytes;
static UmicomSize umicomRegistryChecks;
static UmicomSize umicomRegistryCases;

static void UmicomRegistryRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomRegistryChecks;
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=registry-");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(210U);
        UmicomPlatformHalt();
    }
}
static void UmicomRegistryExpect(UmicomKernelRegistryStatus actual,
    UmicomKernelRegistryStatus expected, const char *reason)
{
    /* Expected refusals are successful evidence, not failure log messages.
     * Only an unexpected status prints the failure marker used by CTest. */
    if (actual != expected) {
        UmicomKernelConsoleWrite("registry.unexpected-status=");
        UmicomKernelConsoleWriteLine(UmicomKernelRegistryStatusName(actual));
    }
    UmicomRegistryRequire(actual == expected ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomRegistryNumber(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static void UmicomRegistryCase(const char *name)
{
    UmicomKernelConsoleWrite("registry.case=");
    UmicomKernelConsoleWriteLine(name);
}
static void UmicomRegistryCasePassed(void)
{
    ++umicomRegistryCases;
    UmicomKernelConsoleWriteLine("registry.case-result=pass");
}
static UmicomKernelProcessHandle UmicomRegistryCreateSample(UmicomU64 owner)
{
    UmicomKernelProcessHandle handle = 0U;
    UmicomRegistryExpect(UmicomKernelProcessRegistryCreate(&umicomAcceptanceRegistry,
        owner, UmicomEmbeddedExecutableStart, umicomRegistryImageBytes,
        UMICOM_PROCESS_RIGHT_ALL, &handle), UMICOM_REGISTRY_OK, "create");
    return handle;
}
static UmicomKernelProcessInfo UmicomRegistryReadSample(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomKernelProcessInfo info;
    UmicomRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomAcceptanceRegistry,
        owner, handle, &info), UMICOM_REGISTRY_OK, "query");
    return info;
}
static void UmicomRegistryCloseSample(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomRegistryExpect(UmicomKernelProcessRegistryClose(&umicomAcceptanceRegistry,
        owner, handle), UMICOM_REGISTRY_OK, "close");
}
static void UmicomRegistryEmpty(void)
{
    UmicomKernelRegistrySnapshot snapshot;
    UmicomRegistryExpect(UmicomKernelProcessRegistrySnapshotRead(&umicomAcceptanceRegistry, &snapshot),
        UMICOM_REGISTRY_OK, "independent-reference-count");
    UmicomRegistryRequire(snapshot.objects == 0U && snapshot.handles == 0U &&
        snapshot.retainedWithoutHandles == 0U ? UMICOM_TRUE : UMICOM_FALSE, "empty-registry");
}
static void UmicomRegistryRunSample(UmicomU64 owner, UmicomKernelProcessHandle handle,
    UmicomU64 argument, UmicomKernelProcessState expectedState, UmicomU64 expectedCause)
{
    /* ProcessRun already verifies borrowed machine CSRs and timer restoration.
     * A user fault/deadline is a valid terminal result, not a monitor failure. */
    UmicomRegistryExpect(UmicomKernelProcessRegistryRun(&umicomAcceptanceRegistry,
        owner, handle, argument, 1000000U), UMICOM_REGISTRY_OK, "run");
    const UmicomKernelProcessInfo info = UmicomRegistryReadSample(owner, handle);
    UmicomKernelConsoleWrite("registry.process-state=");
    UmicomKernelConsoleWriteLine(UmicomKernelProcessStateName(info.state));
    UmicomRegistryNumber("registry.process-identity", info.identity);
    UmicomRegistryRequire(info.state == expectedState && info.trapCause == expectedCause &&
        info.quiesced != UMICOM_FALSE ? UMICOM_TRUE : UMICOM_FALSE, "terminal-evidence");
    if (expectedState == UMICOM_PROCESS_EXITED) {
        UmicomRegistryNumber("registry.exit-value", info.exitValue);
        UmicomRegistryRequire(info.exitValue == info.identity + argument + 33U &&
            info.systemCalls == 3U ? UMICOM_TRUE : UMICOM_FALSE, "separate-elf-return");
    }
    /* The process owner still enforces single-use READY semantics. Handles do
     * not turn an exited image into a silently restartable or copied process. */
    UmicomRegistryExpect(UmicomKernelProcessRegistryRun(&umicomAcceptanceRegistry,
        owner, handle, argument, 1000000U), UMICOM_REGISTRY_RUN_FAILED, "terminal-rerun-refused");
}

void UmicomKernelProcessRegistryValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("process-registry-test=begin");
    umicomRegistryImageBytes = (UmicomSize)((UmicomAddress)UmicomEmbeddedExecutableEnd -
        (UmicomAddress)UmicomEmbeddedExecutableStart);
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRegistryRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "baseline");
    UmicomRegistryExpect(UmicomKernelProcessRegistryInitialize(&umicomAcceptanceRegistry),
        UMICOM_REGISTRY_OK, "initialise");

    UmicomRegistryCase("malformed-image-admission");
    UmicomKernelProcessHandle output = 0xfeedU;
    UmicomRegistryExpect(UmicomKernelProcessRegistryCreate(&umicomAcceptanceRegistry, 11U,
        UmicomEmbeddedExecutableStart, 63U, UMICOM_PROCESS_RIGHT_ALL, &output),
        UMICOM_REGISTRY_LOAD_FAILED, "truncated-image-refused");
    UmicomRegistryRequire(output == 0xfeedU ? UMICOM_TRUE : UMICOM_FALSE, "no-failed-admission-token");
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    UmicomRegistryCase("owner-rights-and-shared-lifetime");
    const UmicomKernelProcessHandle original = UmicomRegistryCreateSample(11U);
    UmicomKernelProcessHandle observer = 0U;
    UmicomKernelProcessHandle receiver = 0U;
    UmicomKernelProcessInfo info;
    UmicomRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomAcceptanceRegistry, 12U, original, &info),
        UMICOM_REGISTRY_WRONG_OWNER, "raw-token-not-transferable");
    UmicomRegistryExpect(UmicomKernelProcessRegistryDuplicate(&umicomAcceptanceRegistry, 11U,
        original, UMICOM_PROCESS_RIGHT_QUERY, &observer), UMICOM_REGISTRY_OK, "read-only-duplicate");
    UmicomRegistryExpect(UmicomKernelProcessRegistryRun(&umicomAcceptanceRegistry, 11U,
        observer, 7U, 1000000U), UMICOM_REGISTRY_ACCESS_DENIED, "observer-cannot-run");
    UmicomRegistryExpect(UmicomKernelProcessRegistryRestrict(&umicomAcceptanceRegistry, 11U,
        observer, UMICOM_PROCESS_RIGHT_ALL), UMICOM_REGISTRY_ACCESS_DENIED, "rights-cannot-grow");
    info = UmicomRegistryReadSample(11U, original);
    UmicomRegistryRequire(info.state == UMICOM_PROCESS_READY && info.systemCalls == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "refused-run-did-not-execute");
    UmicomRegistryExpect(UmicomKernelProcessRegistryGrant(&umicomAcceptanceRegistry, 11U,
        original, 12U, UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_RUN, &receiver),
        UMICOM_REGISTRY_OK, "explicit-owner-grant");
    UmicomRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomAcceptanceRegistry, 11U, receiver, &info),
        UMICOM_REGISTRY_WRONG_OWNER, "receiver-token-bound-to-receiver");
    UmicomRegistryRunSample(12U, receiver, 7U, UMICOM_PROCESS_EXITED, 8U);
    UmicomRegistryRequire(UmicomRegistryReadSample(11U, observer).references == 3U
        ? UMICOM_TRUE : UMICOM_FALSE, "three-aliases-one-object");
    UmicomRegistryCloseSample(11U, original);
    UmicomRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomAcceptanceRegistry, 11U, original, &info),
        UMICOM_REGISTRY_INVALID_HANDLE, "closed-original-invalid");
    UmicomSize closed = 0U;
    UmicomRegistryExpect(UmicomKernelProcessRegistryCloseOwner(&umicomAcceptanceRegistry, 11U, &closed),
        UMICOM_REGISTRY_OK, "sender-owner-cleanup");
    UmicomRegistryRequire(closed == 1U && UmicomRegistryReadSample(12U, receiver).references == 1U
        ? UMICOM_TRUE : UMICOM_FALSE, "receiver-reference-survives");
    UmicomRegistryCloseSample(12U, receiver);
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    UmicomRegistryCase("stale-handle-and-faulted-image");
    const UmicomKernelProcessHandle replacement = UmicomRegistryCreateSample(11U);
    UmicomRegistryRequire(replacement != original ? UMICOM_TRUE : UMICOM_FALSE, "generation-advanced");
    UmicomRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomAcceptanceRegistry, 11U, original, &info),
        UMICOM_REGISTRY_INVALID_HANDLE, "stale-token-cannot-reach-new-image");
    UmicomRegistryRunSample(11U, replacement, 2U, UMICOM_PROCESS_FAULTED, 15U);
    UmicomRegistryCloseSample(11U, replacement);
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    UmicomRegistryCase("deadline-and-recovery");
    const UmicomKernelProcessHandle looping = UmicomRegistryCreateSample(11U);
    UmicomRegistryRunSample(11U, looping, 3U, UMICOM_PROCESS_TIMED_OUT,
        ((UmicomU64)1U << 63U) | 7U);
    UmicomRegistryCloseSample(11U, looping);
    const UmicomKernelProcessHandle recovered = UmicomRegistryCreateSample(11U);
    UmicomRegistryRunSample(11U, recovered, 11U, UMICOM_PROCESS_EXITED, 8U);
    UmicomRegistryCloseSample(11U, recovered);
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    UmicomRegistryCase("owner-and-handle-capacity");
    const UmicomKernelProcessHandle shared = UmicomRegistryCreateSample(21U);
    for (UmicomSize count = 1U; count < UMICOM_PROCESS_REGISTRY_OWNER_LIMIT; ++count) {
        UmicomRegistryExpect(UmicomKernelProcessRegistryDuplicate(&umicomAcceptanceRegistry, 21U,
            shared, UMICOM_PROCESS_RIGHT_QUERY, &output), UMICOM_REGISTRY_OK, "fill-owner-quota");
    }
    UmicomRegistryExpect(UmicomKernelProcessRegistryDuplicate(&umicomAcceptanceRegistry, 21U,
        shared, UMICOM_PROCESS_RIGHT_QUERY, &output), UMICOM_REGISTRY_OWNER_LIMIT, "owner-quota-refused");
    for (UmicomU64 owner = 22U; owner <= 24U; ++owner) {
        for (UmicomSize count = 0U; count < UMICOM_PROCESS_REGISTRY_OWNER_LIMIT; ++count) {
            UmicomRegistryExpect(UmicomKernelProcessRegistryGrant(&umicomAcceptanceRegistry, 21U,
                shared, owner, UMICOM_PROCESS_RIGHT_QUERY, &output), UMICOM_REGISTRY_OK, "fill-global-handles");
        }
    }
    UmicomRegistryExpect(UmicomKernelProcessRegistryGrant(&umicomAcceptanceRegistry, 21U,
        shared, 25U, UMICOM_PROCESS_RIGHT_QUERY, &output), UMICOM_REGISTRY_HANDLE_LIMIT, "global-handle-limit");
    UmicomKernelRegistrySnapshot full;
    UmicomRegistryExpect(UmicomKernelProcessRegistrySnapshotRead(&umicomAcceptanceRegistry, &full),
        UMICOM_REGISTRY_OK, "full-table-recount");
    UmicomRegistryRequire(full.objects == 1U && full.handles == 32U ? UMICOM_TRUE : UMICOM_FALSE,
        "many-handles-one-owned-image");
    for (UmicomU64 owner = 21U; owner <= 24U; ++owner) {
        UmicomRegistryExpect(UmicomKernelProcessRegistryCloseOwner(&umicomAcceptanceRegistry, owner, &closed),
            UMICOM_REGISTRY_OK, "close-full-owner-table");
        UmicomRegistryRequire(closed == 8U ? UMICOM_TRUE : UMICOM_FALSE, "owner-close-count");
    }
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    UmicomRegistryCase("bounded-process-admission");
    for (UmicomU64 owner = 31U; owner <= 38U; ++owner) (void)UmicomRegistryCreateSample(owner);
    UmicomRegistryExpect(UmicomKernelProcessRegistryCreate(&umicomAcceptanceRegistry, 39U,
        UmicomEmbeddedExecutableStart, umicomRegistryImageBytes, UMICOM_PROCESS_RIGHT_ALL, &output),
        UMICOM_REGISTRY_PROCESS_LIMIT, "process-slot-limit");
    for (UmicomU64 owner = 31U; owner <= 38U; ++owner) {
        UmicomRegistryExpect(UmicomKernelProcessRegistryCloseOwner(&umicomAcceptanceRegistry, owner, &closed),
            UMICOM_REGISTRY_OK, "destroy-ready-owner");
        UmicomRegistryRequire(closed == 1U ? UMICOM_TRUE : UMICOM_FALSE, "one-object-per-owner");
    }
    UmicomRegistryEmpty();
    UmicomRegistryCasePassed();

    /* Use the old ECALL path once more. A registry run must not leave its
     * private execution vector installed or discard the original trap state. */
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomRegistryRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U &&
        trapAfter.lastCauseCode == UMICOM_RISCV_EXCEPTION_ECALL_M_MODE
        ? UMICOM_TRUE : UMICOM_FALSE, "original-trap-handler");
    UmicomKernelConsoleWriteLine("registry.original-trap-handler=pass");
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRegistryRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        after.allocatedFrames == before.allocatedFrames && after.reservedFrames == before.reservedFrames &&
        after.freeFrames == before.freeFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "final-frame-accounting");
    UmicomKernelConsoleWriteLine("registry.frame-accounting=restored");
    UmicomRegistryNumber("registry.completed-cases", umicomRegistryCases);
    UmicomRegistryNumber("registry.completed-checks", umicomRegistryChecks);
    UmicomKernelConsoleWriteLine("process-registry-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PROCESS_REGISTRY_READY");
}
