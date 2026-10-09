/*-----------------------------------------------------------------------------
 * Umicom Kernel independently linked persistent-file client
 * File: programs/disk_file_client/main.c
 *
 * Exercise the existing copied file ABI from actual U-mode. Every source and
 * destination belongs to this process; file operations resume the retained
 * request context after the machine dispatcher completes the VFS operation.
 * Separate data and namespace processes keep each journey inside the existing
 * native system-call budget. The data writer deliberately leaves descriptions
 * open at EXIT, making terminal cleanup observable before memory collection.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/file_abi.h"

alignas(4096) static volatile UmicomU8 umicomKernelDiskFileBytes[8192];
static const volatile char umicomKernelDiskFileDirectory[] = "/WORK";
static const volatile char umicomKernelDiskFileName[] = "/WORK/LOG.BIN";
static const volatile char umicomKernelDiskFileTemporary[] = "/WORK/TEMP.TXT";
static const volatile char umicomKernelDiskFileChild[] = "/WORK/SUB";
UmicomU64 UmicomKernelDiskFileProgramCall(const UmicomKernelFileRequest *request,
    UmicomKernelFileResult *result);

static UmicomU64 UmicomKernelDiskFilePointer(const volatile void *pointer)
{
    return (UmicomU64)(UmicomUIntPtr)pointer;
}
static void UmicomKernelDiskFileClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static UmicomU64 UmicomKernelDiskFileIssue(UmicomU64 operation, UmicomU64 descriptor,
    UmicomU64 address, UmicomU64 bytes, UmicomU64 argument, UmicomU64 options,
    UmicomKernelFileResult *result)
{
    const UmicomKernelFileRequest request = {operation, descriptor, address, bytes, argument, options};
    *result = (UmicomKernelFileResult){0U, 0U};
    return UmicomKernelDiskFileProgramCall(&request, result);
}
static UmicomU8 UmicomKernelDiskFileExpected(UmicomSize index)
{
    if (index >= 480U && index < 560U) return (UmicomU8)(((index - 480U) * 17U + 0x91U) & 255U);
    if (index < 700U) return (UmicomU8)((index * 29U + 0x31U) & 255U);
    if (index < 1100U) return (UmicomU8)(((index - 700U) * 53U + 0xb7U) & 255U);
    return 0U;
}
UmicomU64 UmicomKernelDiskFileProgramMain(UmicomU64 mode)
{
    UmicomKernelFileResult result = {0U, 0U};
    const UmicomU64 path = UmicomKernelDiskFilePointer(umicomKernelDiskFileName);
    const UmicomU64 directoryPath = UmicomKernelDiskFilePointer(umicomKernelDiskFileDirectory);
    const UmicomU64 temporaryPath = UmicomKernelDiskFilePointer(umicomKernelDiskFileTemporary);
    const UmicomU64 childPath = UmicomKernelDiskFilePointer(umicomKernelDiskFileChild);
    const UmicomU64 data = UmicomKernelDiskFilePointer(&umicomKernelDiskFileBytes[4080U]);
    const UmicomU64 readRights = UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY;
    const UmicomU64 writeRights = readRights | UMICOM_VFS_RIGHT_WRITE | UMICOM_VFS_RIGHT_DUPLICATE;
    if (mode == 1U) {
        if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, path, sizeof(umicomKernelDiskFileName)-1U,
            readRights, 0U, &result) != UMICOM_VFS_OK) return 0xed01U;
        const UmicomU64 reader = result.value;
        if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ, reader, data, 900U, 0U, 0U, &result)
            != UMICOM_VFS_OK || result.value != 900U) return 0xed02U;
        for (UmicomSize i = 0U; i < 900U; ++i)
            if (umicomKernelDiskFileBytes[4080U+i] != UmicomKernelDiskFileExpected(i)) return 0xed03U;
        if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, reader, data, 1U, 0U, 0U, &result)
            != UMICOM_VFS_ACCESS_DENIED) return 0xed04U;
        if (UmicomKernelDiskFileIssue(UMICOM_FILE_CREATE, 0U, temporaryPath,
            sizeof(umicomKernelDiskFileTemporary)-1U, UMICOM_VFS_FILE, 0U, &result)
            != UMICOM_VFS_ACCESS_DENIED) return 0xed05U;
        if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, path, sizeof(umicomKernelDiskFileName)-1U,
            UMICOM_VFS_RIGHT_WRITE, 0U, &result) != UMICOM_VFS_ACCESS_DENIED) return 0xed06U;
        return 0x7201U; /* The Kernel must close the reader before collection. */
    }
    /* Namespace work is a new process lifetime, not an extension of the native
     * per-process call budget. It opens its own directory description below. */
    if (mode == 2U) goto umicomKernelDiskFileNamespace;
    if (mode != 0U) return 0xed00U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CREATE, 0U, directoryPath,
        sizeof(umicomKernelDiskFileDirectory)-1U, UMICOM_VFS_DIRECTORY, 0U, &result)
        != UMICOM_VFS_OK) return 0xed10U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CREATE, 0U, path, sizeof(umicomKernelDiskFileName)-1U,
        UMICOM_VFS_FILE, 0U, &result) != UMICOM_VFS_OK) return 0xed11U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, path, sizeof(umicomKernelDiskFileName)-1U,
        writeRights, 0U, &result) != UMICOM_VFS_OK) return 0xed12U;
    const UmicomU64 writer = result.value;
    /* An invalid user source must be refused before any provider mutation or
     * shared-position change, even though this process holds write authority. */
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, writer, 0U, 1U, 0U, 0U, &result)
        != UMICOM_FILE_BAD_USER_BUFFER) return 0xed13U;
    for (UmicomSize i = 0U; i < 700U; ++i)
        umicomKernelDiskFileBytes[4080U+i] = (UmicomU8)((i * 29U + 0x31U) & 255U);
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, writer, data, 700U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 700U) return 0xed14U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_SEEK, writer, 0U, 0U, 480U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed15U;
    for (UmicomSize i = 0U; i < 80U; ++i)
        umicomKernelDiskFileBytes[4080U+i] = (UmicomU8)((i * 17U + 0x91U) & 255U);
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, writer, data, 80U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 80U) return 0xed16U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, path, sizeof(umicomKernelDiskFileName)-1U,
        UMICOM_VFS_RIGHT_WRITE, 1U, &result) != UMICOM_VFS_OK) return 0xed17U;
    const UmicomU64 appender = result.value;
    for (UmicomSize i = 0U; i < 400U; ++i)
        umicomKernelDiskFileBytes[4080U+i] = (UmicomU8)((i * 53U + 0xb7U) & 255U);
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, appender, data, 400U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 400U) return 0xed18U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CLOSE, appender, 0U, 0U, 0U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed19U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_DUPLICATE, writer, 0U, 0U, readRights, 0U, &result)
        != UMICOM_VFS_OK) return 0xed1aU;
    const UmicomU64 duplicate = result.value;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, duplicate, data, 1U, 0U, 0U, &result)
        != UMICOM_VFS_ACCESS_DENIED) return 0xed1bU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_RESIZE, writer, 0U, 0U, 1400U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed1cU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_SEEK, writer, 0U, 0U, 1100U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed1dU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ, duplicate, data, 300U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 300U) return 0xed1eU;
    for (UmicomSize i = 0U; i < 300U; ++i) if (umicomKernelDiskFileBytes[4080U+i]) return 0xed1fU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_RESIZE, writer, 0U, 0U, 900U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed20U;
    UmicomKernelFileInfo info = {0U, 0U, 0U, 0U};
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_QUERY, writer, UmicomKernelDiskFilePointer(&info),
        sizeof(info), 0U, 0U, &result) != UMICOM_VFS_OK || info.bytes != 900U ||
        info.kind != UMICOM_VFS_FILE) return 0xed21U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_SEEK, writer, 0U, 0U, 0U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed22U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ, duplicate, data, 900U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 900U) return 0xed23U;
    for (UmicomSize i = 0U; i < 900U; ++i)
        if (umicomKernelDiskFileBytes[4080U+i] != UmicomKernelDiskFileExpected(i)) return 0xed24U;
    return 0x7200U; /* Original and duplicate share a pin until process cleanup. */

umicomKernelDiskFileNamespace:
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, directoryPath,
        sizeof(umicomKernelDiskFileDirectory)-1U, UMICOM_VFS_RIGHT_ENUMERATE, 0U, &result)
        != UMICOM_VFS_OK) return 0xed25U;
    const UmicomU64 directory = result.value;
    UmicomKernelFileEntry entry;
    UmicomKernelDiskFileClear(&entry, sizeof(entry));
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ_DIRECTORY, directory,
        UmicomKernelDiskFilePointer(&entry), sizeof(entry), 0U, 0U, &result)
        != UMICOM_VFS_OK || entry.name[0] != 'L') return 0xed26U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CREATE, 0U, temporaryPath,
        sizeof(umicomKernelDiskFileTemporary)-1U, UMICOM_VFS_FILE, 0U, &result)
        != UMICOM_VFS_OK) return 0xed27U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ_DIRECTORY, directory,
        UmicomKernelDiskFilePointer(&entry), sizeof(entry), 0U, 0U, &result)
        != UMICOM_VFS_CHANGED) return 0xed28U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_OPEN, 0U, temporaryPath,
        sizeof(umicomKernelDiskFileTemporary)-1U, UMICOM_VFS_RIGHT_WRITE, 0U, &result)
        != UMICOM_VFS_OK) return 0xed29U;
    const UmicomU64 temporary = result.value;
    for (UmicomSize i = 0U; i < 32U; ++i) umicomKernelDiskFileBytes[4080U+i] = (UmicomU8)(i + 1U);
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_WRITE, temporary, data, 32U, 0U, 0U, &result)
        != UMICOM_VFS_OK || result.value != 32U) return 0xed2aU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_REMOVE, 0U, temporaryPath,
        sizeof(umicomKernelDiskFileTemporary)-1U, UMICOM_VFS_FILE, 0U, &result)
        != UMICOM_VFS_BUSY) return 0xed2bU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CLOSE, temporary, 0U, 0U, 0U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed2cU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_REMOVE, 0U, temporaryPath,
        sizeof(umicomKernelDiskFileTemporary)-1U, UMICOM_VFS_FILE, 0U, &result)
        != UMICOM_VFS_OK) return 0xed2dU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CREATE, 0U, childPath,
        sizeof(umicomKernelDiskFileChild)-1U, UMICOM_VFS_DIRECTORY, 0U, &result)
        != UMICOM_VFS_OK) return 0xed2eU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_REMOVE, 0U, childPath,
        sizeof(umicomKernelDiskFileChild)-1U, UMICOM_VFS_DIRECTORY, 0U, &result)
        != UMICOM_VFS_OK) return 0xed2fU;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_REWIND_DIRECTORY, directory, 0U, 0U, 0U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed30U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ_DIRECTORY, directory,
        UmicomKernelDiskFilePointer(&entry), sizeof(entry), 0U, 0U, &result)
        != UMICOM_VFS_OK || entry.name[0] != 'L') return 0xed31U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_READ_DIRECTORY, directory,
        UmicomKernelDiskFilePointer(&entry), sizeof(entry), 0U, 0U, &result)
        != UMICOM_VFS_END) return 0xed32U;
    if (UmicomKernelDiskFileIssue(UMICOM_FILE_CLOSE, directory, 0U, 0U, 0U, 0U, &result)
        != UMICOM_VFS_OK) return 0xed33U;
    return 0x7202U;
}
