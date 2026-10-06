/* Umicom Kernel file-owner internals. These helpers share checked copies and
 * ownership resolution between admission and completion; no backend or frame
 * allocator is duplicated. Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_KERNEL_USER_FILES_INTERNAL_H
#define UMICOM_KERNEL_USER_FILES_INTERNAL_H
#include "umicom/kernel/user_files.h"
void UmicomFileClear(void *target, UmicomSize bytes);
UmicomKernelUserFileRecord *UmicomFileRecord(UmicomKernelUserFiles *files, UmicomKernelUserTask *task);
UmicomKernelVfsClient *UmicomFileClient(UmicomKernelUserFiles *files, UmicomKernelUserFileRecord *record);
UmicomU64 UmicomFileReadUser(const UmicomKernelUserMemory *memory, UmicomAddress address,
    void *destination, UmicomSize bytes);
UmicomU64 UmicomFileWriteUser(const UmicomKernelUserMemory *memory, UmicomAddress address,
    const void *source, UmicomSize bytes);
UmicomU64 UmicomFileExecute(UmicomKernelUserFileRecord *record,
    UmicomKernelVfsClient *client, const UmicomKernelUserMemory *memory);
/* A binding exists only between scheduler Begin/End; both pointers identify
 * trusted owners, not values accepted from a program. */
extern UmicomKernelUserFiles *umicomFileBoundOwner;
extern UmicomKernelUserTask *umicomFileBoundTask;
#endif /* UMICOM_KERNEL_USER_FILES_INTERNAL_H */
