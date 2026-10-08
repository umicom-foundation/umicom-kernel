/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_lifecycle_query.h
 *
 * Inspect committed filesystem values through the existing exclusive lease.
 * The query borrows its owner's read transport without acquiring a second
 * device handle or granting access to an unfinished staged namespace.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_LIFECYCLE_QUERY_H
#define UMICOM_KERNEL_FAT16_LIFECYCLE_QUERY_H
#include "umicom/kernel/fat16_lifecycle_commit.h"
#include "umicom/kernel/fat16_metadata.h"

typedef enum UmicomKernelFat16QueryKind {
    UMICOM_FAT16_QUERY_NONE,
    UMICOM_FAT16_QUERY_STAT,
    UMICOM_FAT16_QUERY_LIST,
    UMICOM_FAT16_QUERY_READ
} UmicomKernelFat16QueryKind;

/* STAT and LIST require zero offset/capacity. READ accepts a byte offset and
 * capacity 1..4096. Reading at or beyond EOF succeeds with zero returned bytes.
 * A terminated absolute short-name path is borrowed only for this call. */
typedef struct UmicomKernelFat16Query {
    UmicomKernelFat16QueryKind kind;
    const char *path;
    UmicomU64 offset;
    UmicomSize capacity;
} UmicomKernelFat16Query;

/* No field points into the owner. Unused fields, padding and the unread data
 * tail are zero. Metadata describes the entry on disk, including its persisted
 * write timestamp; committedOperations identifies the local accepted count,
 * not a globally unique filesystem generation or an authentication token. */
typedef struct UmicomKernelFat16QueryResult {
    UmicomKernelFat16QueryKind kind;
    UmicomU64 committedOperations;
    UmicomKernelFat16Metadata metadata;
    UmicomKernelFat16Directory directory;
    UmicomU64 offset;
    UmicomSize bytes;
    UmicomU8 data[UMICOM_FAT16_READ_BYTES];
} UmicomKernelFat16QueryResult;

/* READY and COMMITTED owners may inspect; STAGED, FAILED and closed owners
 * refuse without I/O. No WRITE, FLUSH, Finish or new device lease is submitted.
 * The inspector is freshly opened and closed for each query, under one finite
 * I/O/deadline budget. It retains the ordinary inspector's local checks, not
 * the lifecycle planner's complete allocation-ownership proof.
 *
 * Naturally aligned request/result objects and the terminated path must have
 * independent non-overflowing spans, separate from the owner/domain/DMA pages.
 * Inputs are copied before callbacks; callbacks must not mutate or retain them
 * or re-enter any active owner. These are trusted Kernel pointers, not a user
 * memory probing interface. Every error preserves the entire caller result.
 * Queries preserve lastResult, commit.lastResult and the accepted count, so
 * browsing cannot replace the evidence for the last filesystem mutation.
 * Transport diagnostic fields may change. Close still releases resources only.
 */
UmicomKernelFat16UpdateStatus UmicomKernelFat16LifecycleQuery(
    UmicomKernelFat16LifecycleCommitter *owner,
    const UmicomKernelFat16Query *query,
    UmicomKernelFat16QueryResult *outResult);
#endif /* UMICOM_KERNEL_FAT16_LIFECYCLE_QUERY_H */
