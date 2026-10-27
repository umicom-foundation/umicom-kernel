/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/boot_memory.h
 *
 * PURPOSE:
 *   Copy and clear bounded boot records without requiring a hosted C runtime.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_BOOT_MEMORY_H
#define UMICOM_BOOT_MEMORY_H
#include "umicom/kernel/types.h"

/* Boot callers own valid byte ranges and supply their exact sizeof bounds.
 * Volatile byte accesses prevent loop recognition from lowering these routines
 * to memcpy or memset before a C runtime exists. This is ordinary RAM access,
 * not an MMIO interface; copying requires nonoverlapping ranges. */
static inline void UmicomBootMemoryClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static inline void UmicomBootMemoryCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    const volatile UmicomU8 *in = (const volatile UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
#endif
