/* Umicom Kernel filesystem-local byte helpers.
 * The freestanding Kernel has no hosted string runtime. Keep bounded byte work
 * explicit, including scrubs which must survive dead-store elimination.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_KERNEL_VFS_INTERNAL_H
#define UMICOM_KERNEL_VFS_INTERNAL_H
#include "umicom/kernel/vfs.h"
static inline void UmicomVfsClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static inline UmicomBoolean UmicomVfsZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const in = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (in[i] != 0U) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static inline void UmicomVfsCopy(void *target, const void *source, UmicomSize bytes)
{
    UmicomU8 *const out = (UmicomU8 *)target;
    const UmicomU8 *const in = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static inline UmicomBoolean UmicomVfsNameValid(const char *name)
{
    if (name == (const char *)0 || name[0] == '\0') return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_VFS_NAME_BYTES; ++i) {
        const unsigned char value = (unsigned char)name[i];
        if (value == 0U) {
            /* Relative traversal is a distinct future contract, not a name. */
            if (name[0] == '.' && (i == 1U || (i == 2U && name[1] == '.'))) return UMICOM_FALSE;
            return UMICOM_TRUE;
        }
        if (value < 32U || value > 126U || value == '/' || value == '\\') return UMICOM_FALSE;
    }
    return UMICOM_FALSE;
}
static inline UmicomBoolean UmicomVfsNameEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < UMICOM_VFS_NAME_BYTES; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (a[i] == '\0') return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
#endif /* UMICOM_KERNEL_VFS_INTERNAL_H */
