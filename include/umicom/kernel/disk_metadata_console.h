/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_metadata_console.h
 *
 * Observe persisted FAT16 write metadata through a fresh read-only transport
 * lifetime. The pathname is copied before any callback. Complete metadata is
 * printed only after inspection and resource release both succeed. An absent
 * or invalid calendar remains an explicit observation, never an invented time.
 *
 * The existing disk inspector owns any handle retained after failed cleanup;
 * diskclose or a later inspection retries release. Calls are trusted serial
 * Kernel work. The medium stays immutable through the entire operation, and
 * callback/context storage must remain valid. Output callbacks may not edit
 * the adapter or transport storage. Reentry is refused through final output.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_METADATA_CONSOLE_H
#define UMICOM_KERNEL_DISK_METADATA_CONSOLE_H
#include "umicom/kernel/disk_console.h"
#include "umicom/kernel/fat16_metadata.h"

/* slot and partition select the established read-only inspection profile.
 * Paths use the bounded short-name grammar. No mount, writable lease, WRITE,
 * FLUSH or writer-history lookup is performed. One 4096-read command budget
 * spans Open and metadata lookup, with a monotonic ten-second deadline on the
 * qualified 10 MHz platform. A close error returns IO_ERROR and suppresses
 * metadata output, while the final status line records the cleanup result. */
UmicomKernelDiskStatus UmicomKernelDiskMetadataInspect(UmicomSize slot,
    UmicomSize partition, const char *path, void *context, UmicomKernelBlockOutput output);

#endif /* UMICOM_KERNEL_DISK_METADATA_CONSOLE_H */
