/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/object_cache_validation.c
 *
 * PURPOSE:
 *   Exercise dynamic service-sized records against the real physical allocator.
 *
 * EDUCATIONAL NOTE:
 *   The records resemble file/service metadata, but no filesystem is implied.
 *   All caches are static owners; their backing frames are allocated only by
 *   the tests. No prior service's metadata is moved or rewritten. Deliberate
 *   guard corruption and backend-release injection belong to the native suite,
 *   because a poisoned cache intentionally retains memory for diagnosis.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"

/* These owners cannot move after initialisation. Unused metadata costs static
 * Kernel storage; physical object data is acquired lazily and returned below. */
static UmicomKernelObjectCache umicomRecordCache;
static UmicomKernelObjectCache umicomPageCache;
static UmicomKernelObjectCache umicomOtherCache;
static UmicomKernelObjectReference umicomRecordReferences[40];
static UmicomU64 umicomObjectChecks;
static UmicomU64 umicomObjectCases;

static void UmicomObjectExpect(UmicomBoolean condition, const char *reason)
{
    ++umicomObjectChecks;
    if (condition != UMICOM_FALSE) return;
    UmicomKernelConsoleWrite("object-cache.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x75U);
    UmicomPlatformHalt();
}
static void UmicomObjectCase(const char *name)
{
    ++umicomObjectCases;
    UmicomKernelConsoleWrite("object-cache.case=");
    UmicomKernelConsoleWriteLine(name);
}
static UmicomBoolean UmicomObjectMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    /* Compare meaningful fields, not any padding a C implementation may add. */
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}

void UmicomKernelObjectCachesValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("object-caches-test=begin");
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomKernelPhysicalMemorySnapshot baseline;
    UmicomKernelPhysicalMemorySnapshot final;
    UmicomObjectExpect(UmicomKernelPhysicalMemorySnapshotRead(&baseline) == UMICOM_KERNEL_MEMORY_OK, "read initial frame accounting");

    UmicomObjectCase("lazy-cache-and-frame-observation");
    UmicomObjectExpect(UmicomKernelObjectCacheInitialize(&umicomRecordCache, 64U, 64U, 4U) == UMICOM_OBJECT_OK,
        "create record cache without acquiring a frame");
    UmicomKernelObjectCacheInfo info;
    UmicomObjectExpect(UmicomKernelObjectCacheSnapshot(&umicomRecordCache, &info) == UMICOM_OBJECT_OK &&
        info.frames == 0U && info.liveObjects == 0U && info.slotsPerFrame == 32U, "empty cache geometry");
    UmicomKernelPhysicalFrameState frameState;
    UmicomObjectExpect(UmicomKernelPhysicalMemoryFrameQuery(baseline.ramBase, &frameState) == UMICOM_KERNEL_MEMORY_OK &&
        frameState == UMICOM_PHYSICAL_FRAME_RESERVED, "bootstrap RAM remains reserved");

    UmicomObjectCase("shared-slots-and-noncontiguous-frames");
    UmicomAddress interveningFrame = 0U;
    for (UmicomSize index = 0U; index < 40U; ++index) {
        if (index == 32U) {
            /* Occupy the next free page with an unrelated owner. The cache's
             * second frame must not depend on physical adjacency to its first. */
            UmicomObjectExpect(UmicomKernelPhysicalMemoryAllocateFrame(&interveningFrame) == UMICOM_KERNEL_MEMORY_OK,
                "reserve an unrelated allocated frame between cache pages");
        }
        UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomRecordCache, &umicomRecordReferences[index]) == UMICOM_OBJECT_OK,
            "allocate an independent record");
        void *record = (void *)0;
        UmicomObjectExpect(UmicomKernelObjectCacheResolve(&umicomRecordCache, umicomRecordReferences[index], &record) == UMICOM_OBJECT_OK &&
            ((UmicomAddress)record % 64U) == 0U, "resolve aligned live ticket");
        UmicomU8 *const bytes = (UmicomU8 *)record;
        for (UmicomSize byte = 0U; byte < 64U; ++byte) {
            UmicomObjectExpect(bytes[byte] == 0U, "new record does not disclose previous frame data");
            bytes[byte] = (UmicomU8)(index + byte);
        }
    }
    const UmicomAddress firstFrame = (UmicomAddress)umicomRecordReferences[0].address;
    const UmicomAddress secondFrame = (UmicomAddress)umicomRecordReferences[32].address;
    UmicomObjectExpect(secondFrame != firstFrame + UMICOM_KERNEL_PAGE_SIZE &&
        secondFrame != interveningFrame, "backing frames are not assumed contiguous");
    UmicomObjectExpect(UmicomKernelObjectCacheSnapshot(&umicomRecordCache, &info) == UMICOM_OBJECT_OK &&
        info.frames == 2U && info.liveObjects == 40U && info.liveBytes == 2560U, "two frames contain forty records");
    UmicomKernelConsoleWriteLine("object-cache.noncontiguous-backing=pass");

    UmicomObjectCase("checked-reference-and-stale-reuse");
    UmicomObjectExpect(UmicomKernelObjectCacheClose(&umicomRecordCache) == UMICOM_OBJECT_BUSY,
        "live records prevent close");
    UmicomKernelObjectReference invalid = umicomRecordReferences[0];
    invalid.address = (void *)((UmicomAddress)invalid.address + 1U);
    UmicomObjectExpect(UmicomKernelObjectCacheFree(&umicomRecordCache, invalid) == UMICOM_OBJECT_INVALID_REFERENCE,
        "an interior address is not an allocation reference");
    const UmicomKernelObjectReference stale = umicomRecordReferences[0];
    UmicomObjectExpect(UmicomKernelObjectCacheFree(&umicomRecordCache, stale) == UMICOM_OBJECT_OK &&
        UmicomKernelObjectCacheFree(&umicomRecordCache, stale) == UMICOM_OBJECT_INVALID_REFERENCE, "double free refused");
    UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomRecordCache, &umicomRecordReferences[0]) == UMICOM_OBJECT_OK &&
        umicomRecordReferences[0].address == stale.address && umicomRecordReferences[0].ticket != stale.ticket,
        "reused address receives a new allocation ticket");
    UmicomObjectExpect(UmicomKernelObjectCacheFree(&umicomRecordCache, stale) == UMICOM_OBJECT_INVALID_REFERENCE,
        "old reference cannot free the replacement record");
    for (UmicomSize index = 0U; index < 40U; ++index) {
        void *record = (void *)0;
        UmicomObjectExpect(UmicomKernelObjectCacheResolve(&umicomRecordCache, umicomRecordReferences[index], &record) == UMICOM_OBJECT_OK,
            "remaining live record resolves");
        const UmicomU8 *const bytes = (const UmicomU8 *)record;
        for (UmicomSize byte = 0U; byte < 64U; ++byte) {
            const UmicomU8 expected = index == 0U ? 0U : (UmicomU8)(index + byte);
            UmicomObjectExpect(bytes[byte] == expected, "object contents remain independent");
        }
        UmicomObjectExpect(UmicomKernelObjectCacheFree(&umicomRecordCache, umicomRecordReferences[index]) == UMICOM_OBJECT_OK,
            "release record before releasing backing frames");
    }
    UmicomKernelConsoleWriteLine("object-cache.stale-reference=refused");

    UmicomObjectCase("explicit-trim-and-page-alignment");
    UmicomObjectExpect(UmicomKernelPhysicalMemoryFrameQuery(firstFrame, &frameState) == UMICOM_KERNEL_MEMORY_OK &&
        frameState == UMICOM_PHYSICAL_FRAME_ALLOCATED, "free records leave cache ownership intact");
    UmicomSize released = 0U;
    UmicomObjectExpect(UmicomKernelObjectCacheTrim(&umicomRecordCache, &released) == UMICOM_OBJECT_OK && released == 2U,
        "trim releases exactly the two empty cache pages");
    UmicomObjectExpect(UmicomKernelPhysicalMemoryFrameQuery(firstFrame, &frameState) == UMICOM_KERNEL_MEMORY_OK &&
        frameState == UMICOM_PHYSICAL_FRAME_FREE, "trim returned page ownership");
    UmicomObjectExpect(UmicomKernelPhysicalMemoryFreeFrame(interveningFrame) == UMICOM_KERNEL_MEMORY_OK,
        "unrelated owner releases its own frame");
    UmicomObjectExpect(UmicomKernelObjectCacheClose(&umicomRecordCache) == UMICOM_OBJECT_OK, "close empty cache");
    UmicomObjectExpect(UmicomKernelObjectCacheInitialize(&umicomPageCache, 4080U, 4096U, 1U) == UMICOM_OBJECT_OK,
        "page-aligned object retains a sixteen-byte diagnostic tail");
    UmicomKernelObjectReference large = {0};
    UmicomKernelObjectReference refused = {0};
    UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomPageCache, &large) == UMICOM_OBJECT_OK &&
        ((UmicomAddress)large.address % 4096U) == 0U, "largest supported record is page aligned");
    UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomPageCache, &refused) == UMICOM_OBJECT_CAPACITY,
        "capacity refuses rather than evicting the live object");
    UmicomObjectExpect(UmicomKernelObjectCacheInitialize(&umicomOtherCache, 32U, 16U, 1U) == UMICOM_OBJECT_OK &&
        UmicomKernelObjectCacheFree(&umicomOtherCache, large) == UMICOM_OBJECT_INVALID_REFERENCE,
        "a different cache cannot release this reference");

    UmicomObjectCase("unfinished-critical-section-refusal");
    UmicomKernelCriticalSection section = 0U;
    UmicomObjectExpect(UmicomKernelCriticalSectionEnter(0x7501U, &section) == UMICOM_INTERRUPT_OK,
        "enter managed section for allocation-refusal check");
    UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomOtherCache, &refused) == UMICOM_OBJECT_UNSAFE_CONTEXT,
        "allocation cannot hide unfinished interrupt ownership");
    UmicomObjectExpect(UmicomKernelCriticalSectionLeave(0x7501U, section) == UMICOM_INTERRUPT_OK,
        "release the same managed section");
    UmicomObjectExpect(UmicomKernelObjectCacheAllocate(&umicomOtherCache, &refused) == UMICOM_OBJECT_OK &&
        UmicomKernelObjectCacheFree(&umicomOtherCache, refused) == UMICOM_OBJECT_OK, "normal allocation works after release");
    UmicomObjectExpect(UmicomKernelObjectCacheFree(&umicomPageCache, large) == UMICOM_OBJECT_OK &&
        UmicomKernelObjectCacheClose(&umicomPageCache) == UMICOM_OBJECT_OK &&
        UmicomKernelObjectCacheClose(&umicomOtherCache) == UMICOM_OBJECT_OK, "explicitly collect remaining cache frames");

    /* The ownership layer may advance diagnostic counters, but it must not
     * leave a changed hardware control policy or lose a physical allocation. */
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomObjectExpect(UmicomObjectMachineEqual(&machineBefore, &machineAfter), "machine control state is unchanged");
    UmicomObjectExpect(UmicomKernelPhysicalMemorySnapshotRead(&final) == UMICOM_KERNEL_MEMORY_OK &&
        final.allocatedFrames == baseline.allocatedFrames && final.reservedFrames == baseline.reservedFrames &&
        final.freeFrames == baseline.freeFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "all dynamic frames return to the pre-cache baseline");
    UmicomRiscvTrapSnapshot beforeTrap;
    UmicomRiscvTrapSnapshot afterTrap;
    UmicomRiscvTrapSnapshotRead(&beforeTrap);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&afterTrap);
    UmicomObjectExpect(afterTrap.exceptionCount == beforeTrap.exceptionCount + 1U, "existing ECALL handler still returns");
    UmicomKernelConsoleWriteLine("object-cache.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("object-cache.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("object-cache.frame-accounting=restored");
    UmicomKernelConsoleWrite("object-cache.completed-cases=");
    UmicomKernelConsoleWriteUnsigned(umicomObjectCases);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWrite("object-cache.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomObjectChecks);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("object-caches-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_OBJECT_CACHES_READY");
}
