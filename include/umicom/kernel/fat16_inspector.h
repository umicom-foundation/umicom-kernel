/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_inspector.h
 *
 * A bounded, read-only FAT16 inspector over a checked primary MBR partition.
 * It is not a filesystem repair utility, a writable VFS provider or an EFI
 * firmware implementation. The original RAMFS remains the writable namespace.
 *
 * Start with zero-filled, stable Kernel storage. Caller result buffers must
 * not overlap this owner or its input storage. These are trusted Kernel C
 * interfaces, not a user-pointer ABI.
 *
 * This owner caches scratch bytes, not authority to reset a device. Keep the
 * reader and its context valid until Close, then close the original transport.
 * The medium must remain unchanged from Open through Close. No operation
 * allocates physical memory or retains a caller's output pointer.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_INSPECTOR_H
#define UMICOM_KERNEL_FAT16_INSPECTOR_H
#include "umicom/kernel/disk_inspection.h"

#define UMICOM_FAT16_PATH_BYTES 256U
#define UMICOM_FAT16_DEPTH_LIMIT 8U
#define UMICOM_FAT16_ENTRY_LIMIT 128U
#define UMICOM_FAT16_SCAN_ENTRIES 512U
#define UMICOM_FAT16_CHAIN_LIMIT 256U
#define UMICOM_FAT16_IO_LIMIT 4096U
#define UMICOM_FAT16_READ_BYTES 4096U

typedef struct UmicomKernelFat16Entry {
    char name[13]; /* Canonical uppercase short alias; no borrowed disk string. */
    UmicomBoolean directory;
    UmicomU32 bytes;
    UmicomU16 firstCluster;
    UmicomU8 attributes;
} UmicomKernelFat16Entry;
typedef struct UmicomKernelFat16Directory {
    UmicomSize count;
    UmicomSize longNameRecords; /* Skipped, never interpreted as short entries. */
    UmicomKernelFat16Entry entries[UMICOM_FAT16_ENTRY_LIMIT];
} UmicomKernelFat16Directory;
typedef struct UmicomKernelFat16Info {
    UmicomU64 firstSector;
    UmicomU64 volumeSectors;
    UmicomU32 clusters;
    UmicomU16 sectorsPerFat;
    UmicomU16 rootEntries;
    UmicomU8 sectorsPerCluster;
    char label[12]; /* Sanitised metadata, not an authenticated volume name. */
} UmicomKernelFat16Info;

typedef struct UmicomKernelFat16 {
    const struct UmicomKernelFat16 *self;
    UmicomKernelDiskReader reader;
    UmicomKernelFat16Info info;
    UmicomU64 partitionSectors;
    UmicomU64 fatStart;
    UmicomU64 rootStart;
    UmicomU64 dataStart;
    UmicomBoolean open;
    UmicomBoolean busy;
    /* Scratch belongs to this stable owner. An error never leaks a prefix of
     * a directory listing or Read result into the caller's output buffer. */
    UmicomU8 fatSector[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 mirrorSector[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 dataSector[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 readStage[UMICOM_FAT16_READ_BYTES];
    UmicomKernelFat16Directory directoryStage;
    UmicomU64 cachedFatSector;
    UmicomBoolean fatCached;
    UmicomSize reads;
} UmicomKernelFat16;

/* Open checks both MBR and BPB geometry, deriving FAT type from cluster count,
 * never from a label. This profile requires two agreeing FATs, 512-byte sectors
 * and clean FAT16 flags. A failed Open leaves the owner unchanged. An owner may
 * reopen after Close, but it must not be copied or concurrently accessed. */
UmicomKernelDiskStatus UmicomKernelFat16Open(UmicomKernelFat16 *volume,
    const UmicomKernelDiskReader *reader, UmicomSize partition);
UmicomKernelDiskStatus UmicomKernelFat16Close(UmicomKernelFat16 *volume);
/* Inputs use absolute paths and the short 8.3 alias alphabet. ASCII case is
 * folded; empty components, . and .. are refused before any directory I/O.
 * These outputs are value snapshots, not executable or descriptor authority. */
UmicomKernelDiskStatus UmicomKernelFat16Stat(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Entry *outEntry);
UmicomKernelDiskStatus UmicomKernelFat16List(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Directory *outDirectory);
/* Read at most 4096 bytes; validate the complete bounded file chain first.
 * On error both output and outBytes stay unchanged. At or beyond EOF an OK
 * result reports zero. The unused tail of output is never overwritten. */
UmicomKernelDiskStatus UmicomKernelFat16Read(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, void *output, UmicomSize capacity, UmicomSize *outBytes);
#endif /* UMICOM_KERNEL_FAT16_INSPECTOR_H */
