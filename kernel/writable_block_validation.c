/* Umicom Kernel bounded WRITE/FLUSH qualification on an owned disposable disk.
 * The writer checks every original byte before publishing its first mutation.
 * A separately started read-only guest checks every resulting byte after QEMU
 * has closed the backend. This establishes the exercised transport/flush and
 * orderly reopen behavior; it is not a power-cut or filesystem durability test.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/writable_block_validation.h"
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/virtio_block/writable_fixture_format.h"

#if defined(UMICOM_KERNEL_WRITABLE_BLOCK_TEST) == defined(UMICOM_KERNEL_BLOCK_WRITE_READBACK_TEST)
#error "Select exactly one dedicated writable-block qualification image"
#endif

#define UMICOM_WRITE_INPUT_GUARD_BYTES 32U
#define UMICOM_WRITE_BUFFER_BYTES (UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES)
static UmicomU8 umicomWriteValidationInput[UMICOM_WRITE_BUFFER_BYTES + 2U * UMICOM_WRITE_INPUT_GUARD_BYTES];
static UmicomU8 umicomWriteValidationReadback[UMICOM_WRITE_BUFFER_BYTES];

static void UmicomWriteLocalFill(void *memory, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *p = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) p[i] = value;
}
static void UmicomWriteExpect(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("block-write-validation.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x88U); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomWriteStatus(UmicomKernelBlockStatus actual, UmicomKernelBlockStatus expected,
    const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("block-write-validation.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomWriteExpect(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomWriteMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomWriteDiscover(UmicomSize *outIndex)
{
    UmicomKernelBlockDomain *domain = 0;
    UmicomWriteStatus(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomWriteLocalFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outIndex = i; ++found; }
        else UmicomWriteExpect(status == UMICOM_BLOCK_NO_DEVICE || status == UMICOM_BLOCK_NOT_BLOCK ||
            status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT, "advertised transport identity");
    }
    UmicomWriteExpect(found == 1U, "exactly one dedicated block fixture device");
    return domain;
}
static void UmicomWriteProbe(UmicomKernelBlockDomain *domain, UmicomSize index,
    UmicomKernelBlockInfo *info)
{
    UmicomWriteLocalFill(info, sizeof(*info), 0U);
    UmicomWriteStatus(UmicomKernelBlockProbe(domain, index, info), UMICOM_BLOCK_OK,
        "owned transport can be inspected");
    UmicomWriteExpect(info->sectors == UMICOM_BLOCK_FIXTURE_SECTORS && info->heldFrames == 2U &&
        info->deviceMayAccessMemory, "fixture capacity and two live DMA frames");
}
static void UmicomWriteCheckDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomBoolean changed)
{
    for (UmicomU64 first = 0U; first < UMICOM_BLOCK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        const UmicomSize sectors = UMICOM_BLOCK_FIXTURE_SECTORS - first < UMICOM_BLOCK_MAX_SECTORS ?
            (UmicomSize)(UMICOM_BLOCK_FIXTURE_SECTORS - first) : UMICOM_BLOCK_MAX_SECTORS;
        UmicomWriteLocalFill(umicomWriteValidationReadback, sizeof(umicomWriteValidationReadback), 0xa5U);
        UmicomWriteStatus(UmicomKernelBlockRead(domain, handle, first, sectors,
            umicomWriteValidationReadback, sizeof(umicomWriteValidationReadback)), UMICOM_BLOCK_OK,
            "whole fixture read");
        for (UmicomSize i = 0U; i < sectors * UMICOM_BLOCK_SECTOR_BYTES; ++i) {
            const UmicomU64 sector = first + i / UMICOM_BLOCK_SECTOR_BYTES;
            const UmicomSize offset = i % UMICOM_BLOCK_SECTOR_BYTES;
            const UmicomU8 expected = changed ? UmicomBlockWriteFixtureByte(sector, offset) :
                UmicomBlockFixtureByte(sector, offset);
            UmicomWriteExpect(umicomWriteValidationReadback[i] == expected,
                changed ? "changed ranges and every untouched sector" : "entire original fixture before mutation");
        }
    }
}
static UmicomU8 *UmicomWriteInput(void)
{
    return umicomWriteValidationInput + UMICOM_WRITE_INPUT_GUARD_BYTES;
}
static void UmicomWritePrepareInput(UmicomU64 first, UmicomSize sectors)
{
    UmicomWriteLocalFill(umicomWriteValidationInput, sizeof(umicomWriteValidationInput), 0xa7U);
    UmicomU8 *input = UmicomWriteInput();
    for (UmicomSize i = 0U; i < sectors * UMICOM_BLOCK_SECTOR_BYTES; ++i)
        input[i] = UmicomBlockWriteFixtureByte(first + i / UMICOM_BLOCK_SECTOR_BYTES,
            i % UMICOM_BLOCK_SECTOR_BYTES);
}
static void UmicomWriteCheckInput(UmicomU64 first, UmicomSize sectors)
{
    const UmicomSize bytes = sectors * UMICOM_BLOCK_SECTOR_BYTES;
    for (UmicomSize i = 0U; i < sizeof(umicomWriteValidationInput); ++i) {
        UmicomU8 expected = 0xa7U;
        if (i >= UMICOM_WRITE_INPUT_GUARD_BYTES && i - UMICOM_WRITE_INPUT_GUARD_BYTES < bytes) {
            const UmicomSize offset = i - UMICOM_WRITE_INPUT_GUARD_BYTES;
            expected = UmicomBlockWriteFixtureByte(first + offset / UMICOM_BLOCK_SECTOR_BYTES,
                offset % UMICOM_BLOCK_SECTOR_BYTES);
        }
        UmicomWriteExpect(umicomWriteValidationInput[i] == expected,
            "const input and both surrounding guards unchanged");
    }
}
static void UmicomWriteAccounting(const UmicomKernelPhysicalMemorySnapshot *before)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomWriteLocalFill(&after, sizeof(after), 0U);
    UmicomWriteExpect(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "all DMA ownership returned");
}

#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
static void UmicomWriteRange(UmicomKernelBlockDomain *domain, UmicomSize index,
    UmicomKernelBlockHandle handle, UmicomU64 first, UmicomSize sectors)
{
    UmicomWritePrepareInput(first, sectors);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    UmicomWriteStatus(UmicomKernelBlockWrite(domain, handle, first, sectors, UmicomWriteInput(),
        sectors * UMICOM_BLOCK_SECTOR_BYTES, &outcome), UMICOM_BLOCK_OK, "bounded WRITE completion");
    UmicomWriteExpect(outcome == UMICOM_BLOCK_COMPLETED, "WRITE completion is explicit");
    UmicomWriteCheckInput(first, sectors);
    UmicomKernelBlockInfo info;
    UmicomWriteProbe(domain, index, &info);
    UmicomWriteExpect(info.writable && info.needsFlush && !info.writeUncertain,
        "completed WRITE still requires a FLUSH");
}
static void UmicomWriteFixture(UmicomKernelBlockDomain *domain, UmicomSize index,
    UmicomKernelBlockHandle handle)
{
    UmicomWriteCheckDisk(domain, handle, UMICOM_FALSE);
    UmicomKernelConsoleWriteLine("block-write.original-fixture=fully-verified");
    UmicomKernelBlockInfo before, after;
    UmicomWriteProbe(domain, index, &before);
    UmicomWriteExpect(before.writable && !before.needsFlush && !before.writeUncertain,
        "new writable lease starts without submitted mutations");
    UmicomWritePrepareInput(UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_COMPLETED;
    UmicomWriteStatus(UmicomKernelBlockWrite(domain, handle, UMICOM_BLOCK_FIXTURE_SECTORS, 1U,
        UmicomWriteInput(), UMICOM_BLOCK_SECTOR_BYTES, &outcome), UMICOM_BLOCK_RANGE,
        "outside-capacity WRITE refused before publication");
    UmicomWriteExpect(outcome == UMICOM_BLOCK_NOT_SUBMITTED, "invalid WRITE was not submitted");
    UmicomWriteCheckInput(UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U);
    UmicomWriteProbe(domain, index, &after);
    UmicomWriteExpect(after.requests == before.requests && !after.needsFlush && !after.writeUncertain,
        "preflight refusal has no queue or mutation effect");
    UmicomKernelConsoleWriteLine("block-write.bounds=refused-before-submission");
    UmicomWriteRange(domain, index, handle, UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U);
    UmicomWriteRange(domain, index, handle, UMICOM_BLOCK_WRITE_FULL_FIRST_SECTOR,
        UMICOM_BLOCK_WRITE_FULL_SECTORS);
    for (UmicomU64 sector = UMICOM_BLOCK_WRITE_REUSE_FIRST_SECTOR;
         sector < UMICOM_BLOCK_WRITE_REUSE_FIRST_SECTOR + UMICOM_BLOCK_WRITE_REUSE_SECTORS; ++sector)
        UmicomWriteRange(domain, index, handle, sector, 1U);
    UmicomWriteRange(domain, index, handle, UMICOM_BLOCK_WRITE_LAST_SECTOR, 1U);
    UmicomKernelConsoleWriteLine("block-write.single-page-final-sector-and-ring-reuse=verified");
    UmicomKernelConsoleWriteLine("block-write.input-and-guards=unchanged");
    UmicomWriteCheckDisk(domain, handle, UMICOM_TRUE);
    UmicomWriteProbe(domain, index, &after);
    UmicomWriteExpect(after.needsFlush && !after.writeUncertain,
        "readback does not claim a flush barrier");
    outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    UmicomWriteStatus(UmicomKernelBlockFlush(domain, handle, &outcome), UMICOM_BLOCK_OK,
        "FLUSH after all WRITE completions");
    UmicomWriteExpect(outcome == UMICOM_BLOCK_COMPLETED, "FLUSH completion is explicit");
    UmicomWriteProbe(domain, index, &after);
    UmicomWriteExpect(after.writable && !after.needsFlush && !after.writeUncertain,
        "successful FLUSH clears pending durability state");
    UmicomKernelConsoleWriteLine("block-write.flush=completed");
    UmicomKernelConsoleWriteLine("block-write.all-written-and-untouched-bytes=verified");
}
#else
static void UmicomWriteReadOnlyRefusals(UmicomKernelBlockDomain *domain, UmicomSize index,
    UmicomKernelBlockHandle handle)
{
    UmicomKernelBlockInfo before, after;
    UmicomWriteProbe(domain, index, &before);
    UmicomWriteExpect(!before.writable && !before.needsFlush && !before.writeUncertain,
        "fresh readback lease remains read-only");
    UmicomWritePrepareInput(UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_COMPLETED;
    UmicomWriteStatus(UmicomKernelBlockWrite(domain, handle, UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U,
        UmicomWriteInput(), UMICOM_BLOCK_SECTOR_BYTES, &outcome), UMICOM_BLOCK_READ_ONLY,
        "read-only lease refuses WRITE");
    UmicomWriteExpect(outcome == UMICOM_BLOCK_NOT_SUBMITTED, "read-only WRITE was not submitted");
    UmicomWriteCheckInput(UMICOM_BLOCK_WRITE_FIRST_SECTOR, 1U);
    outcome = UMICOM_BLOCK_COMPLETED;
    UmicomWriteStatus(UmicomKernelBlockFlush(domain, handle, &outcome), UMICOM_BLOCK_READ_ONLY,
        "read-only lease refuses FLUSH");
    UmicomWriteExpect(outcome == UMICOM_BLOCK_NOT_SUBMITTED, "read-only FLUSH was not submitted");
    UmicomWriteProbe(domain, index, &after);
    UmicomWriteExpect(after.requests == before.requests && !after.needsFlush && !after.writeUncertain,
        "read-only mutation calls leave the queue and state unchanged");
    UmicomKernelConsoleWriteLine("block-write-readback.mutation-authority=refused");
}
#endif

void UmicomKernelWritableBlockValidateExecution(void)
{
#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
    UmicomKernelConsoleWriteLine("writable-block-test=begin");
#else
    UmicomKernelConsoleWriteLine("block-write-readback-test=begin");
#endif
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomWriteLocalFill(&before, sizeof(before), 0U);
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomWriteLocalFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomWriteLocalFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomWriteExpect(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "allocator available before transport ownership");
    UmicomSize index = 0U;
    UmicomKernelBlockDomain *domain = UmicomWriteDiscover(&index);
    UmicomKernelBlockHandle handle = 0U;
#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
    UmicomWriteStatus(UmicomKernelBlockOpen(domain, index, 10000000U, &handle), UMICOM_BLOCK_WRITABLE_DEVICE,
        "original read-only Open still refuses a writable backend");
    UmicomWriteExpect(handle == 0U, "refused read-only admission retains no lease");
    UmicomWriteAccounting(&before);
    UmicomWriteStatus(UmicomKernelBlockOpenWritable(domain, index, 10000000U, &handle), UMICOM_BLOCK_OK,
        "explicit modern writable admission with FLUSH");
    UmicomKernelConsoleWriteLine("block-write.separate-admission=verified");
    UmicomWriteFixture(domain, index, handle);
#else
    UmicomWriteStatus(UmicomKernelBlockOpenWritable(domain, index, 10000000U, &handle), UMICOM_BLOCK_READ_ONLY,
        "writable admission refuses the read-only cold backend");
    UmicomWriteExpect(handle == 0U, "refused writable admission retains no lease");
    UmicomWriteAccounting(&before);
    UmicomWriteStatus(UmicomKernelBlockOpen(domain, index, 10000000U, &handle), UMICOM_BLOCK_OK,
        "fresh read-only transport admission");
    UmicomWriteReadOnlyRefusals(domain, index, handle);
    UmicomWriteCheckDisk(domain, handle, UMICOM_TRUE);
    UmicomKernelConsoleWriteLine("block-write-readback.all-persisted-and-untouched-bytes=verified");
#endif
    const UmicomKernelBlockHandle stale = handle;
    UmicomWriteStatus(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK,
        "device reset precedes DMA release");
    UmicomWriteStatus(UmicomKernelBlockClose(domain, stale), UMICOM_BLOCK_INVALID_HANDLE,
        "closed lease cannot be reused");
    UmicomWriteAccounting(&before);
    handle = 0U;
#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
    UmicomWriteStatus(UmicomKernelBlockOpenWritable(domain, index, 10000000U, &handle), UMICOM_BLOCK_OK,
        "writable transport reopens after an observed reset");
#else
    UmicomWriteStatus(UmicomKernelBlockOpen(domain, index, 10000000U, &handle), UMICOM_BLOCK_OK,
        "read-only transport reopens after an observed reset");
#endif
    UmicomWriteExpect(handle != stale, "reopened transport uses a fresh generation");
    UmicomWriteCheckDisk(domain, handle, UMICOM_TRUE);
    UmicomWriteStatus(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK, "final transport close");
    UmicomWriteAccounting(&before);
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomWriteExpect(UmicomWriteMachineEqual(&machineBefore, &machineAfter), "machine controls unchanged");
#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
    UmicomKernelConsoleWriteLine("block-write.reset-reopen-and-stale-lease=verified");
    UmicomKernelConsoleWriteLine("block-write.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("block-write.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("writable-block-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_WRITABLE_BLOCK_READY");
#else
    UmicomKernelConsoleWriteLine("block-write-readback.reset-reopen-and-stale-lease=verified");
    UmicomKernelConsoleWriteLine("block-write-readback.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("block-write-readback.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("block-write-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BLOCK_WRITE_READBACK_READY");
#endif
}
