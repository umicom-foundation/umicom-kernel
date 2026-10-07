/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_metadata.h
 *
 * Read a persisted file's short-entry metadata through the checked FAT16
 * inspector. The timestamp is a value copied from media, independent of any
 * writer owner's retained result. Raw words remain visible when the calendar
 * is absent or malformed; no timestamp grants authority to execute or write.
 *
 * The existing Entry and VFS layouts remain unchanged. This header reuses the
 * explicit calendar value from the file planner without acquiring a planner,
 * writable transport or commit lifetime. No operation here modifies media.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_METADATA_H
#define UMICOM_KERNEL_FAT16_METADATA_H
#include "umicom/kernel/fat16_file_plan.h"

typedef enum UmicomKernelFat16TimestampState {
    UMICOM_FAT16_TIMESTAMP_ABSENT,
    UMICOM_FAT16_TIMESTAMP_VALID,
    UMICOM_FAT16_TIMESTAMP_INVALID
} UmicomKernelFat16TimestampState;

/* FAT records local calendar fields without a timezone. rawTime and rawDate
 * are the original little-endian words decoded to host integer values. value
 * is entirely zero unless state is VALID; no zero word becomes an epoch. */
typedef struct UmicomKernelFat16Timestamp {
    UmicomU16 rawTime;
    UmicomU16 rawDate;
    UmicomKernelFat16FileTime value;
    UmicomKernelFat16TimestampState state;
} UmicomKernelFat16Timestamp;

typedef struct UmicomKernelFat16Metadata {
    UmicomKernelFat16Entry entry;
    /* The FAT16 root has no containing short directory entry. Its synthetic
     * root Entry is retained, with this flag false and an ABSENT timestamp.
     * A normal file or subdirectory has a real entry even when its time words
     * are zero. Callers distinguish those cases using this explicit flag. */
    UmicomBoolean directoryEntryPresent;
    UmicomKernelFat16Timestamp writeTimestamp;
} UmicomKernelFat16Metadata;

/* Classify any pair of raw words without media access or allocation. Both
 * words zero means ABSENT. A real Gregorian date in 1980..2107 and a valid
 * hour/minute/even second means VALID. Every other pair means INVALID, with
 * its raw words preserved. Encoded 60/62 seconds are invalid, not leap seconds.
 * Midnight has a zero time word and remains valid when its date is valid.
 *
 * A non-null, aligned, non-overflowing output object is required. Invalid
 * storage returns INVALID_ARGUMENT without changing any output byte. Calendar
 * content is always reported by state with OK, including ABSENT and INVALID.
 * No timezone conversion or calendar convention is inferred. */
UmicomKernelDiskStatus UmicomKernelFat16TimestampDecode(UmicomU16 rawTime,
    UmicomU16 rawDate, UmicomKernelFat16Timestamp *outTimestamp);

/* Copy the selected file or directory's Entry and persisted write timestamp.
 * Root / succeeds with directoryEntryPresent false, a synthetic root Entry and
 * a zero ABSENT timestamp. Malformed time words are inspectable as INVALID;
 * they do not turn an otherwise valid directory record into an I/O failure.
 * Creation and access fields are outside this metadata query's contract.
 *
 * volume, outMetadata and the terminated path must have independent, checked
 * non-overflowing extents; typed objects require their natural alignment.
 * These are trusted Kernel pointers, not user-memory probing. The path is
 * copied before the first callback. Every error leaves the entire output
 * unchanged, including padding; no pointer into the inspector is returned.
 *
 * The existing short-name path grammar, complete parent-directory decoding,
 * bounded parent-chain validation and one total 4096-read budget apply. This
 * retains the ordinary read-only profile: long-name records are skipped and
 * no global allocation proof or target-file data-chain validation is implied.
 * The medium must remain immutable from the inspector's Open through Close.
 * Reader callbacks must not edit caller/owner storage, retain pointers or
 * re-enter the active owner; they must enforce their finite transport bound.
 * No operation allocates memory, writes, flushes or closes the transport. */
UmicomKernelDiskStatus UmicomKernelFat16MetadataRead(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Metadata *outMetadata);

void UmicomKernelFat16MetadataValidate(void);
void UmicomKernelFat16MetadataCommittedValidate(void);
void UmicomKernelFat16MetadataDirtyValidate(void);
_Noreturn void UmicomKernelFat16MetadataBoot(UmicomU64 hart, UmicomAddress deviceTree);

#endif /* UMICOM_KERNEL_FAT16_METADATA_H */
