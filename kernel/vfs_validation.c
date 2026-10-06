/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/vfs_validation.c
 *
 * PURPOSE:
 *   Exercise real RAM-backed files and descriptor lifetimes, then feed file
 *   bytes to the existing ELF loader and execute the resulting private image.
 *
 * EDUCATIONAL OVERVIEW:
 *   The filesystem is not an executable loader. It stores and returns bytes;
 *   the existing loader remains responsible for validating ELF and owning user
 *   pages. Destroying this entire filesystem before the program runs proves
 *   that the process did not borrow its instruction bytes from a file buffer.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/ramfs.h"
#include "umicom/kernel/process.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "vfs_internal.h"

extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
static UmicomKernelRamfs umicomFileStore;
static UmicomKernelVfs umicomNamespace;
static UmicomKernelVfsClient umicomFileClient;
static UmicomKernelVfsClient umicomObserverClient;
static UmicomKernelProcess umicomFileProcess;
static UmicomU8 umicomFileBytes[UMICOM_RAMFS_FILE_BYTES];
static UmicomU8 umicomFileSample[8208];
static UmicomU8 umicomFileReadback[8208];
static UmicomU64 umicomVfsChecks;
static UmicomU64 umicomVfsCases;

static void UmicomVfsExpect(UmicomBoolean condition, const char *reason)
{
    ++umicomVfsChecks;
    if (condition) return;
    UmicomKernelConsoleWrite("vfs.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x86U);
    UmicomPlatformHalt();
}
static void UmicomVfsExpectStatus(UmicomKernelVfsStatus status, const char *reason)
{
    if (status != UMICOM_VFS_OK) {
        UmicomKernelConsoleWrite("vfs.status=");
        UmicomKernelConsoleWriteLine(UmicomKernelVfsStatusName(status));
    }
    UmicomVfsExpect(status == UMICOM_VFS_OK ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomVfsCase(const char *name)
{
    ++umicomVfsCases;
    UmicomKernelConsoleWrite("vfs.case=");
    UmicomKernelConsoleWriteLine(name);
}
static UmicomBoolean UmicomVfsBytesEqual(const UmicomU8 *a, const UmicomU8 *b, UmicomSize bytes)
{
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomVfsMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    /* Padding is not machine state. Compare the control fields individually. */
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}
void UmicomKernelVfsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("vfs-ramfs-test=begin");
    UmicomKernelPhysicalMemorySnapshot baseline;
    UmicomVfsClear(&baseline, sizeof(baseline));
    UmicomKernelPhysicalMemorySnapshot final;
    UmicomVfsClear(&final, sizeof(final));
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    UmicomVfsExpect(UmicomKernelPhysicalMemorySnapshotRead(&baseline) == UMICOM_KERNEL_MEMORY_OK, "initial frame accounting");
    UmicomKernelFileDescriptor journal = 0U;
    UmicomKernelFileDescriptor reader = 0U;
    UmicomKernelFileDescriptor duplicate = 0U;
    UmicomKernelFileDescriptor directory = 0U;
    UmicomKernelFileDescriptor program = 0U;
    UmicomSize count = 0U;
    const UmicomKernelVfsRights fileRights = UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE |
        UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE;

    UmicomVfsCase("lazy-mount-and-bounded-namespace");
    UmicomVfsExpectStatus(UmicomKernelRamfsInitialize(&umicomFileStore), "initialise RAMFS");
    UmicomVfsExpectStatus(UmicomKernelVfsMount(&umicomNamespace, UmicomKernelRamfsOperations(), &umicomFileStore), "pin mounted root");
    UmicomVfsExpectStatus(UmicomKernelVfsClientOpen(&umicomFileClient, &umicomNamespace, 101U, UMICOM_VFS_RIGHT_ALL), "create trusted client");
    UmicomVfsExpectStatus(UmicomKernelVfsClientOpen(&umicomObserverClient, &umicomNamespace, 202U, UMICOM_VFS_RIGHT_READ), "create read-only client");
    UmicomKernelRamfsInfo storeInfo;
    UmicomVfsClear(&storeInfo, sizeof(storeInfo));
    UmicomVfsExpectStatus(UmicomKernelRamfsSnapshot(&umicomFileStore, &storeInfo), "read lazy state");
    UmicomVfsExpect(storeInfo.nodes == 1U && storeInfo.dataPages == 0U && storeInfo.metadataPages == 0U, "mount allocates no physical frame");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bank", UMICOM_VFS_DIRECTORY), "create bank directory");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bank/accounts", UMICOM_VFS_DIRECTORY), "create account directory");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bank/accounts/journal", UMICOM_VFS_FILE), "create journal");
    UmicomVfsExpect(UmicomKernelVfsCreate(&umicomFileClient, "/bank/../escape", UMICOM_VFS_FILE) == UMICOM_VFS_INVALID_PATH, "traversal is not a canonical path");
    UmicomVfsExpectStatus(UmicomKernelVfsOpen(&umicomFileClient, "/bank/accounts/journal", fileRights, UMICOM_FALSE, &journal), "open journal");

    UmicomVfsCase("cross-page-data-holes-and-resize");
    for (UmicomSize i = 0U; i < sizeof(umicomFileSample); ++i) umicomFileSample[i] = (UmicomU8)(i * 23U + 1U);
    UmicomVfsExpectStatus(UmicomKernelVfsSeek(&umicomFileClient, journal, 4091U), "seek beyond initial EOF");
    UmicomVfsExpectStatus(UmicomKernelVfsWrite(&umicomFileClient, journal, umicomFileSample, sizeof(umicomFileSample), &count), "write across page boundaries");
    UmicomVfsExpect(count == sizeof(umicomFileSample), "report exact written bytes");
    UmicomVfsExpectStatus(UmicomKernelVfsSeek(&umicomFileClient, journal, 0U), "read sparse prefix");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, journal, umicomFileReadback, 4091U, &count), "copy sparse prefix");
    UmicomVfsExpect(count == 4091U && UmicomVfsZero(umicomFileReadback, count), "seek gap is zero-filled");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, journal, umicomFileReadback, sizeof(umicomFileReadback), &count), "copy journal payload");
    UmicomVfsExpect(count == sizeof(umicomFileSample) && UmicomVfsBytesEqual(umicomFileSample, umicomFileReadback, count), "independent pages contain the exact payload");
    UmicomVfsExpectStatus(UmicomKernelVfsResize(&umicomFileClient, journal, 4096U), "shrink journal");
    UmicomVfsExpectStatus(UmicomKernelVfsResize(&umicomFileClient, journal, 8192U), "regrow journal");
    UmicomVfsExpectStatus(UmicomKernelVfsSeek(&umicomFileClient, journal, 4096U), "seek regrown region");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, journal, umicomFileReadback, 4096U, &count), "read regrown region");
    UmicomVfsExpect(count == 4096U && UmicomVfsZero(umicomFileReadback, count), "truncated bytes never reappear");
    UmicomKernelConsoleWriteLine("vfs.cross-page-and-zero-fill=pass");

    UmicomVfsCase("descriptor-rights-and-shared-position");
    UmicomVfsExpectStatus(UmicomKernelVfsSeek(&umicomFileClient, journal, 4091U), "select remaining journal prefix");
    UmicomVfsExpectStatus(UmicomKernelVfsDuplicate(&umicomFileClient, journal, UMICOM_VFS_RIGHT_READ, &duplicate), "duplicate with fewer rights");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, journal, umicomFileReadback, 2U, &count), "advance shared position");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, duplicate, umicomFileReadback, 3U, &count), "duplicate resumes at shared position");
    UmicomVfsExpect(count == 3U && UmicomVfsBytesEqual(umicomFileReadback, umicomFileSample + 2U, 3U), "duplicate did not reopen at offset zero");
    UmicomVfsExpect(UmicomKernelVfsWrite(&umicomFileClient, duplicate, umicomFileSample, 1U, &count) == UMICOM_VFS_ACCESS_DENIED, "read-only duplicate cannot write");
    UmicomVfsExpectStatus(UmicomKernelVfsOpen(&umicomObserverClient, "/bank/accounts/journal", UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &reader), "other client independently opens journal");
    UmicomVfsExpect(UmicomKernelVfsRemove(&umicomObserverClient, "/bank/accounts/journal", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED, "read authority is not remove authority");
    UmicomKernelConsoleWriteLine("vfs.descriptor-rights-and-offsets=pass");

    UmicomVfsCase("directory-iteration-and-change-detection");
    UmicomVfsExpectStatus(UmicomKernelVfsOpen(&umicomFileClient, "/bank/accounts", UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE, &directory), "open directory cursor");
    UmicomKernelVfsDirectoryEntry entry;
    UmicomVfsClear(&entry, sizeof(entry));
    UmicomVfsExpectStatus(UmicomKernelVfsReadDirectory(&umicomFileClient, directory, &entry), "enumerate first child");
    UmicomVfsExpect(UmicomVfsNameEqual(entry.name, "journal") && entry.info.bytes == 8192U, "enumeration returns a named value snapshot");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bank/accounts/settings", UMICOM_VFS_FILE), "change namespace after cursor starts");
    UmicomVfsExpect(UmicomKernelVfsReadDirectory(&umicomFileClient, directory, &entry) == UMICOM_VFS_CHANGED, "old cursor cannot silently skip a change");
    UmicomVfsExpectStatus(UmicomKernelVfsRewindDirectory(&umicomFileClient, directory), "restart enumeration explicitly");
    UmicomSize entries = 0U;
    UmicomKernelVfsStatus enumerated = UMICOM_VFS_OK;
    while ((enumerated = UmicomKernelVfsReadDirectory(&umicomFileClient, directory, &entry)) == UMICOM_VFS_OK) ++entries;
    UmicomVfsExpect(enumerated == UMICOM_VFS_END && entries == 2U, "both named children are visible");

    UmicomVfsCase("unlink-keeps-open-image-and-stale-tokens-fail");
    UmicomVfsExpectStatus(UmicomKernelVfsRemove(&umicomFileClient, "/bank/accounts/journal", UMICOM_VFS_FILE), "remove journal name");
    UmicomVfsExpect(UmicomKernelVfsOpen(&umicomFileClient, "/bank/accounts/journal", fileRights, UMICOM_FALSE, &program) == UMICOM_VFS_NOT_FOUND, "removed name no longer resolves");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomObserverClient, reader, umicomFileReadback, 8U, &count), "open descriptor survives unlink");
    UmicomVfsExpect(count == 8U, "unlinked file still has its old length");
    UmicomVfsExpectStatus(UmicomKernelRamfsReap(&umicomFileStore, &count), "reap only unreachable objects");
    UmicomVfsExpect(count == 0U, "open descriptions pin the unlinked node");
    UmicomVfsExpectStatus(UmicomKernelVfsClose(&umicomFileClient, duplicate), "close duplicate reference");
    UmicomVfsExpectStatus(UmicomKernelVfsClose(&umicomFileClient, journal), "close last writer description");
    UmicomVfsExpectStatus(UmicomKernelVfsClose(&umicomObserverClient, reader), "close final reader pin");
    UmicomVfsExpectStatus(UmicomKernelRamfsReap(&umicomFileStore, &count), "release unreachable journal");
    UmicomVfsExpect(count == 1U, "exactly one node is reclaimed");
    UmicomVfsExpect(UmicomKernelVfsClose(&umicomFileClient, journal) == UMICOM_VFS_INVALID_DESCRIPTOR, "double close is not a different object");
    UmicomKernelConsoleWriteLine("vfs.unlink-open-lifetime=pass");

    UmicomVfsCase("ram-file-bytes-to-existing-executable-loader");
    const UmicomSize imageBytes = (UmicomSize)((UmicomAddress)UmicomEmbeddedExecutableEnd - (UmicomAddress)UmicomEmbeddedExecutableStart);
    UmicomVfsExpect(imageBytes > 0U && imageBytes <= sizeof(umicomFileBytes), "diagnostic ELF fits the explicit file limit");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bin", UMICOM_VFS_DIRECTORY), "create executable directory");
    UmicomVfsExpectStatus(UmicomKernelVfsCreate(&umicomFileClient, "/bin/umicom-diagnostic.elf", UMICOM_VFS_FILE), "create named executable file");
    UmicomVfsExpectStatus(UmicomKernelVfsOpen(&umicomFileClient, "/bin/umicom-diagnostic.elf", fileRights, UMICOM_FALSE, &program), "open executable file");
    UmicomVfsExpectStatus(UmicomKernelVfsWrite(&umicomFileClient, program, UmicomEmbeddedExecutableStart, imageBytes, &count), "copy ELF into RAMFS-owned pages");
    UmicomVfsExpect(count == imageBytes, "store full executable");
    UmicomVfsExpectStatus(UmicomKernelVfsSeek(&umicomFileClient, program, 0U), "rewind executable file");
    UmicomVfsExpectStatus(UmicomKernelVfsRead(&umicomFileClient, program, umicomFileBytes, imageBytes, &count), "read ELF through VFS, not backend pointers");
    UmicomVfsExpect(count == imageBytes && UmicomVfsBytesEqual(umicomFileBytes, UmicomEmbeddedExecutableStart, imageBytes), "file bytes round-trip exactly");
    UmicomKernelExecutableStatus inspected = UMICOM_EXECUTABLE_OK;
    UmicomVfsExpect(UmicomKernelProcessCreate(&umicomFileProcess, umicomFileBytes, imageBytes, 701U, &inspected) == UMICOM_PROCESS_OK, "existing loader owns a private image from VFS bytes");
    /* A private image must not depend on either the file or the staging span. */
    UmicomVfsClear(umicomFileBytes, sizeof(umicomFileBytes));
    UmicomVfsExpectStatus(UmicomKernelVfsClientClose(&umicomObserverClient, &count), "close observer client");
    UmicomVfsExpectStatus(UmicomKernelVfsClientClose(&umicomFileClient, &count), "close client descriptors");
    UmicomVfsExpectStatus(UmicomKernelVfsUnmount(&umicomNamespace), "release mounted root pin");
    UmicomVfsExpectStatus(UmicomKernelRamfsClose(&umicomFileStore), "release filesystem before program executes");
    UmicomVfsExpect(UmicomKernelProcessRun(&umicomFileProcess, 7U, 1000000U) == UMICOM_PROCESS_OK, "run VFS-sourced image through established monitor");
    UmicomVfsExpect(umicomFileProcess.state == UMICOM_PROCESS_EXITED && umicomFileProcess.report.exitValue == 741U &&
        umicomFileProcess.report.trapCause == 8U && ((umicomFileProcess.report.trapStatus >> 11U) & 3U) == 0U,
        "actual user exit proves independent code, BSS and system calls");
    UmicomVfsExpect(UmicomKernelProcessDestroy(&umicomFileProcess) == UMICOM_PROCESS_OK, "destroy private program image");
    UmicomKernelConsoleWriteLine("vfs.file-sourced-executable=pass");

    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomVfsExpect(UmicomVfsMachineEqual(&before, &after), "machine controls restored");
    UmicomVfsExpect(UmicomKernelPhysicalMemorySnapshotRead(&final) == UMICOM_KERNEL_MEMORY_OK &&
        final.allocatedFrames == baseline.allocatedFrames && final.reservedFrames == baseline.reservedFrames &&
        final.freeFrames == baseline.freeFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "all filesystem and executable frames returned");
    UmicomKernelConsoleWriteLine("vfs.machine-state=restored");
    UmicomKernelConsoleWriteLine("vfs.frame-accounting=restored");
    UmicomKernelConsoleWrite("vfs.completed-cases=");
    UmicomKernelConsoleWriteUnsigned(umicomVfsCases);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWrite("vfs.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomVfsChecks);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("vfs-ramfs-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_VFS_RAMFS_READY");
}
