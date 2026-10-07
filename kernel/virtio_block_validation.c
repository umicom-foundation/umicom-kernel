/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/virtio_block_validation.c
 *
 * Exercise the actual modern VirtIO transport and synthetic host fixture. A
 * normal diagnostic run without a disk proves only safe discovery/refusal; it
 * must not emit the read-path acceptance marker. The dedicated CTest attaches
 * the checked fixture read-only and requires actual sector contents and cleanup.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/virtio_block/fixture_format.h"

/* Volatile stores keep freestanding clearing independent of a hosted memset. */
static void UmicomBlockLocalClear(void *memory, UmicomSize bytes)
{
    volatile UmicomU8 *p = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) p[i] = 0U;
}

static UmicomU8 umicomBlockValidationBytes[4096];
static void UmicomBlockExpect(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("block-validation.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x82U);
    UmicomPlatformHalt();
}
static void UmicomBlockCheckBytes(UmicomU64 first, UmicomSize sectors)
{
    for (UmicomSize i = 0U; i < sectors * UMICOM_BLOCK_SECTOR_BYTES; ++i)
        UmicomBlockExpect(umicomBlockValidationBytes[i] ==
            UmicomBlockFixtureByte(first + i / UMICOM_BLOCK_SECTOR_BYTES, i % UMICOM_BLOCK_SECTOR_BYTES),
            "actual disk bytes must match the synthetic fixture");
}
static UmicomBoolean UmicomBlockMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
void UmicomKernelBlockValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("read-only-block-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomBlockLocalClear(&before, sizeof(before));
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomBlockLocalClear(&after, sizeof(after));
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomBlockLocalClear(&machineBefore, sizeof(machineBefore));
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomBlockLocalClear(&machineAfter, sizeof(machineAfter));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomBlockExpect(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "allocator available");
    UmicomKernelBlockDomain *domain = 0;
    const UmicomKernelBlockStatus admitted = UmicomPlatformBlockDomainGet(&domain);
    if (admitted != UMICOM_BLOCK_OK) {
        UmicomKernelConsoleWrite("block.profile=");
        UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(admitted));
    }
    UmicomBlockExpect(admitted == UMICOM_BLOCK_OK, "qualified transport catalogue");
    UmicomSize found = 0U;
    UmicomSize index = 0U;
    for (UmicomSize slot = 0U; slot < domain->count; ++slot) {
        UmicomKernelBlockInfo info;
        UmicomBlockLocalClear(&info, sizeof(info));
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, slot, &info);
        if (status == UMICOM_BLOCK_OK) { index = slot; ++found; }
        else UmicomBlockExpect(status == UMICOM_BLOCK_NO_DEVICE || status == UMICOM_BLOCK_NOT_BLOCK ||
            status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT, "valid advertised transport identity");
    }
    if (found == 0U) {
        UmicomBlockExpect(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
            after.allocatedFrames == before.allocatedFrames, "discovery allocates no DMA pages");
        UmicomKernelConsoleWriteLine("block.read-tests=not-run-no-modern-block-device");
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BLOCK_ABSENT_READY");
        return;
    }
    UmicomBlockExpect(found == 1U, "dedicated disk test expects exactly one fixture device");
    UmicomKernelBlockHandle handle = 0U;
    const UmicomKernelBlockStatus opened = UmicomKernelBlockOpen(domain, index, 10000000U, &handle);
    if (opened != UMICOM_BLOCK_OK) {
        UmicomKernelConsoleWrite("block.open=");
        UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(opened));
    }
    UmicomBlockExpect(opened == UMICOM_BLOCK_OK, "modern read-only feature negotiation");
    UmicomKernelBlockInfo info;
    UmicomBlockLocalClear(&info, sizeof(info));
    UmicomBlockExpect(UmicomKernelBlockProbe(domain, index, &info) == UMICOM_BLOCK_OK &&
        info.sectors == UMICOM_BLOCK_FIXTURE_SECTORS && info.heldFrames == 2U, "fixture capacity and two DMA pages");
    UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, 0U, 1U, umicomBlockValidationBytes,
        sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_OK, "first sector read");
    UmicomBlockCheckBytes(0U, 1U);
    UmicomKernelConsoleWriteLine("block.first-sector=verified");
    UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, 7U, 8U, umicomBlockValidationBytes,
        sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_OK, "full bounce-page read");
    UmicomBlockCheckBytes(7U, 8U);
    UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, UMICOM_BLOCK_FIXTURE_SECTORS - 1U, 1U,
        umicomBlockValidationBytes, sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_OK, "last valid sector");
    UmicomBlockCheckBytes(UMICOM_BLOCK_FIXTURE_SECTORS - 1U, 1U);
    /* More completions than the queue size prove ring-slot reuse, not just the
     * first descriptor chain. Full 16-bit index wrap belongs to the host suite. */
    for (UmicomSize sector = 16U; sector < 28U; ++sector) {
        UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, sector, 1U, umicomBlockValidationBytes,
            sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_OK, "ring-slot reuse");
        UmicomBlockCheckBytes(sector, 1U);
    }
    UmicomKernelConsoleWriteLine("block.multi-sector-and-ring-reuse=verified");
    for (UmicomSize i = 0U; i < sizeof(umicomBlockValidationBytes); ++i) umicomBlockValidationBytes[i] = 0xa5U;
    UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, UMICOM_BLOCK_FIXTURE_SECTORS, 1U,
        umicomBlockValidationBytes, sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_RANGE, "outside capacity refused");
    for (UmicomSize i = 0U; i < sizeof(umicomBlockValidationBytes); ++i)
        UmicomBlockExpect(umicomBlockValidationBytes[i] == 0xa5U, "invalid request cannot touch caller output");
    const UmicomKernelBlockHandle stale = handle;
    UmicomBlockExpect(UmicomKernelBlockClose(domain, handle) == UMICOM_BLOCK_OK, "reset before releasing DMA pages");
    UmicomBlockExpect(UmicomKernelBlockClose(domain, stale) == UMICOM_BLOCK_INVALID_HANDLE, "closed lease is stale");
    handle = 0U;
    UmicomBlockExpect(UmicomKernelBlockOpen(domain, index, 10000000U, &handle) == UMICOM_BLOCK_OK && handle != stale,
        "reopen uses a fresh transport generation");
    UmicomBlockExpect(UmicomKernelBlockRead(domain, handle, 2U, 1U, umicomBlockValidationBytes,
        sizeof(umicomBlockValidationBytes)) == UMICOM_BLOCK_OK, "fresh owner reads after reset");
    UmicomBlockCheckBytes(2U, 1U);
    UmicomBlockExpect(UmicomKernelBlockClose(domain, handle) == UMICOM_BLOCK_OK, "final close");
    UmicomBlockExpect(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "all backing pages returned");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomBlockExpect(UmicomBlockMachineEqual(&machineBefore, &machineAfter), "machine controls unchanged");
    UmicomKernelConsoleWriteLine("block.bounds-and-stale-handles=refused");
    UmicomKernelConsoleWriteLine("block.reset-before-free=verified");
    UmicomKernelConsoleWriteLine("block.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("block.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("block.disk-writes=none");
    UmicomKernelConsoleWriteLine("read-only-block-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_READ_ONLY_BLOCK_READY");
}
