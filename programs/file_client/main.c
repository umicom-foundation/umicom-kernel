/*-----------------------------------------------------------------------------
 * Umicom Kernel independent file client
 * File: programs/file_client/main.c
 *
 * The program has no Kernel pointer and no direct VFS call. Its request record
 * is copied across the native boundary; completion restores this same stack.
 * A writer deliberately leaves descriptions open so terminal cleanup, rather
 * than a well-behaved application's CLOSE loop, is what the guest must prove.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/file_abi.h"

alignas(4096) static volatile UmicomU8 umicomFileBytes[8192];
static const volatile char umicomFileName[] = "/records/journal";
static const volatile char umicomSharedName[] = "/shared/report";
static const volatile char umicomDirectoryName[] = "/records";
UmicomU64 UmicomFileProgramCall(const UmicomKernelFileRequest *request, UmicomKernelFileResult *result);
UmicomU64 UmicomFileProgramIdentity(void);
UmicomU64 UmicomFileProgramFault(void);
void UmicomFileProgramSpin(void);
static void UmicomFileProgramClear(void *target, UmicomSize bytes)
{
    /* A freestanding program has no implicit memset runtime. These stores also
     * initialise every wire-record byte before a const-qualified address call. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static UmicomU64 UmicomFilePointer(const volatile void *pointer)
{
    return (UmicomU64)(UmicomUIntPtr)pointer; /* Address value, not authority to access Kernel memory. */
}
static UmicomU64 UmicomFileIssue(UmicomU64 operation, UmicomU64 descriptor,
    UmicomU64 address, UmicomU64 bytes, UmicomU64 argument, UmicomKernelFileResult *result)
{
    /* Both records are initialised before any const-qualified pointer helper
     * or call. A zero result is not success until the returned status is checked. */
    const UmicomKernelFileRequest request = {operation, descriptor, address, bytes, argument, 0U};
    *result = (UmicomKernelFileResult){0U, 0U};
    return UmicomFileProgramCall(&request, result);
}
UmicomU64 UmicomFileProgramMain(UmicomU64 mode)
{
    UmicomKernelFileResult result = {0};
    const UmicomU64 shared = UmicomFilePointer(umicomSharedName);
    const UmicomU64 data = UmicomFilePointer(&umicomFileBytes[4080]);
    const UmicomU64 rw = UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE |
        UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE;
    if (mode == 5U) {
        if (UmicomFileIssue(UMICOM_FILE_OPEN, 0U, shared, sizeof(umicomSharedName)-1U,
            UMICOM_VFS_RIGHT_READ, &result) != UMICOM_FILE_SERVICE_UNBOUND) return 0xef01U;
        return 0x6500U;
    }
    if (mode != 0U) {
        if (UmicomFileIssue(UMICOM_FILE_OPEN, 0U, shared, sizeof(umicomSharedName)-1U,
            UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, &result) != UMICOM_VFS_OK) return 0xef02U;
        const UmicomU64 descriptor = result.value;
        if (mode == 2U) return UmicomFileProgramFault();
        if (mode == 3U || mode == 4U) { UmicomFileProgramSpin(); return 0xef03U; }
        if (UmicomFileIssue(UMICOM_FILE_READ, descriptor, data, 32U, 0U, &result) != UMICOM_VFS_OK ||
            result.value != 14U || umicomFileBytes[4080] != 'U') return 0xef04U;
        /* A reader cannot acquire WRITE by changing only the operation number. */
        if (UmicomFileIssue(UMICOM_FILE_WRITE, descriptor, data, 1U, 0U, &result) != UMICOM_VFS_ACCESS_DENIED)
            return 0xef05U;
        if (UmicomFileIssue(UMICOM_FILE_CREATE, 0U, UmicomFilePointer(umicomFileName), sizeof(umicomFileName)-1U,
            UMICOM_VFS_FILE, &result) != UMICOM_VFS_ACCESS_DENIED) return 0xef06U;
        return 0x6100U + UmicomFileProgramIdentity(); /* Leave the reader open for Kernel cleanup. */
    }
    const UmicomU64 path = UmicomFilePointer(umicomFileName);
    if (UmicomFileIssue(UMICOM_FILE_CREATE, 0U, path, sizeof(umicomFileName)-1U,
        UMICOM_VFS_FILE, &result) != UMICOM_VFS_OK) return 0xef10U;
    if (UmicomFileIssue(UMICOM_FILE_OPEN, 0U, path, sizeof(umicomFileName)-1U, rw, &result) != UMICOM_VFS_OK)
        return 0xef11U;
    const UmicomU64 descriptor = result.value;
    /* Cross a page boundary in both directions. Filling the source is ordinary
     * user memory work, not writing to a Kernel-owned file buffer. */
    for (UmicomSize i = 0U; i < 64U; ++i) umicomFileBytes[4080U+i] = (UmicomU8)(i+11U);
    if (UmicomFileIssue(UMICOM_FILE_WRITE, descriptor, data, 64U, 0U, &result) != UMICOM_VFS_OK || result.value != 64U)
        return 0xef12U;
    if (UmicomFileIssue(UMICOM_FILE_DUPLICATE, descriptor, 0U, 0U, UMICOM_VFS_RIGHT_READ, &result)
        != UMICOM_VFS_OK) return 0xef13U;
    const UmicomU64 duplicate = result.value;
    if (UmicomFileIssue(UMICOM_FILE_SEEK, descriptor, 0U, 0U, 0U, &result) != UMICOM_VFS_OK) return 0xef14U;
    /* An unmapped output must not advance the shared description's position. */
    if (UmicomFileIssue(UMICOM_FILE_READ, duplicate, 0U, 64U, 0U, &result) != UMICOM_FILE_BAD_USER_BUFFER)
        return 0xef15U;
    for (UmicomSize i = 0U; i < 64U; ++i) umicomFileBytes[4080U+i] = 0xeeU;
    if (UmicomFileIssue(UMICOM_FILE_READ, duplicate, data, 64U, 0U, &result) != UMICOM_VFS_OK || result.value != 64U)
        return 0xef16U;
    for (UmicomSize i = 0U; i < 64U; ++i) if (umicomFileBytes[4080U+i] != (UmicomU8)(i+11U)) return 0xef17U;
    if (UmicomFileIssue(UMICOM_FILE_WRITE, duplicate, data, 1U, 0U, &result) != UMICOM_VFS_ACCESS_DENIED)
        return 0xef18U;
    UmicomKernelFileInfo info = {0};
    if (UmicomFileIssue(UMICOM_FILE_QUERY, descriptor, UmicomFilePointer(&info), sizeof(info), 0U, &result)
        != UMICOM_VFS_OK || info.bytes != 64U || info.kind != UMICOM_VFS_FILE) return 0xef19U;
    if (UmicomFileIssue(UMICOM_FILE_RESIZE, descriptor, 0U, 0U, 128U, &result) != UMICOM_VFS_OK) return 0xef1aU;
    /* Duplicate and original still share position 64. The extension is zeroed. */
    if (UmicomFileIssue(UMICOM_FILE_READ, duplicate, data, 64U, 0U, &result) != UMICOM_VFS_OK || result.value != 64U)
        return 0xef1bU;
    for (UmicomSize i = 0U; i < 64U; ++i) if (umicomFileBytes[4080U+i] != 0U) return 0xef1cU;
    if (UmicomFileIssue(UMICOM_FILE_OPEN, 0U, UmicomFilePointer(umicomDirectoryName), sizeof(umicomDirectoryName)-1U,
        UMICOM_VFS_RIGHT_ENUMERATE, &result) != UMICOM_VFS_OK) return 0xef1dU;
    const UmicomU64 directory = result.value;
    UmicomKernelFileEntry entry;
    UmicomFileProgramClear(&entry, sizeof(entry));
    if (UmicomFileIssue(UMICOM_FILE_READ_DIRECTORY, directory, UmicomFilePointer(&entry), sizeof(entry), 0U, &result)
        != UMICOM_VFS_OK || entry.name[0] != 'j') return 0xef1eU;
    if (UmicomFileIssue(UMICOM_FILE_REMOVE, 0U, path, sizeof(umicomFileName)-1U, UMICOM_VFS_FILE, &result)
        != UMICOM_VFS_OK) return 0xef1fU;
    if (UmicomFileIssue(UMICOM_FILE_QUERY, descriptor, UmicomFilePointer(&info), sizeof(info), 0U, &result)
        != UMICOM_VFS_OK || info.bytes != 128U) return 0xef20U;
    /* Three descriptors intentionally survive this return. EXIT must close
     * them before collection frees the client's private address space. */
    return 0x6000U + UmicomFileProgramIdentity();
}
