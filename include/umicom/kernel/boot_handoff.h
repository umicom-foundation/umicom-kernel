/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/boot_handoff.h
 *
 * PURPOSE:
 *   Admit a bounded firmware-neutral boot description without following physical addresses.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_BOOT_HANDOFF_H
#define UMICOM_KERNEL_BOOT_HANDOFF_H
#include "umicom/kernel/types.h"

#define UMICOM_BOOT_HANDOFF_HEADER_BYTES 64U
#define UMICOM_BOOT_HANDOFF_MAX_BYTES 4096U
#define UMICOM_BOOT_HANDOFF_RAM_LIMIT 32U
#define UMICOM_BOOT_HANDOFF_RESERVED_LIMIT 64U
#define UMICOM_BOOT_HANDOFF_COMMAND_BYTES 256U

typedef enum UmicomBootArchitecture {
    UMICOM_BOOT_RISCV64 = 1, UMICOM_BOOT_X86_64 = 2, UMICOM_BOOT_AARCH64 = 3
} UmicomBootArchitecture;
typedef enum UmicomBootFirmwareTable {
    UMICOM_BOOT_TABLE_NONE = 0, UMICOM_BOOT_TABLE_FDT = 1, UMICOM_BOOT_TABLE_ACPI_RSDP = 2
} UmicomBootFirmwareTable;
typedef enum UmicomBootPixelFormat {
    UMICOM_BOOT_PIXELS_NONE = 0, UMICOM_BOOT_PIXELS_RGBX32 = 1, UMICOM_BOOT_PIXELS_BGRX32 = 2
} UmicomBootPixelFormat;
typedef enum UmicomBootHandoffStatus {
    UMICOM_BOOT_HANDOFF_OK = 0,
    UMICOM_BOOT_HANDOFF_ARGUMENT,
    UMICOM_BOOT_HANDOFF_HEADER,
    UMICOM_BOOT_HANDOFF_UNSUPPORTED,
    UMICOM_BOOT_HANDOFF_LIMIT,
    UMICOM_BOOT_HANDOFF_LENGTH,
    UMICOM_BOOT_HANDOFF_RANGE,
    UMICOM_BOOT_HANDOFF_MEMORY,
    UMICOM_BOOT_HANDOFF_FRAMEBUFFER,
    UMICOM_BOOT_HANDOFF_TEXT
} UmicomBootHandoffStatus;
typedef struct UmicomBootSpan {
    UmicomU64 address;
    UmicomU64 bytes;
} UmicomBootSpan;
typedef struct UmicomBootFramebuffer {
    UmicomBootSpan storage;
    UmicomU32 width;
    UmicomU32 height;
    UmicomU32 strideBytes;
    UmicomBootPixelFormat format;
} UmicomBootFramebuffer;
typedef struct UmicomKernelBootHandoff {
    UmicomBootArchitecture architecture;
    UmicomU64 bootstrapCpu;
    UmicomBootSpan kernel;
    UmicomSize ramCount;
    UmicomSize reservedCount;
    UmicomBootSpan ram[UMICOM_BOOT_HANDOFF_RAM_LIMIT];
    UmicomBootSpan reserved[UMICOM_BOOT_HANDOFF_RESERVED_LIMIT];
    UmicomBootFirmwareTable tableKind;
    UmicomBootSpan table;
    UmicomBootFramebuffer framebuffer;
    char commandLine[UMICOM_BOOT_HANDOFF_COMMAND_BYTES];
} UmicomKernelBootHandoff;

/* The adapter supplies an accessible, immutable input buffer and writable,
 * nonoverlapping output storage. Neither may change during this call. Counts
 * and integer addresses cannot prove that an arbitrary pointer is readable.
 * The parser follows no physical address and changes no allocator or device.
 * On every refusal, out remains byte-for-byte unchanged. On success it owns a
 * complete copy, with unused storage zeroed, and no input-buffer lifetime tie.
 * Architecture tags describe the record; they do not add a boot entry point.
 * See docs/BOOT_HANDOFF.md for the exact little-endian wire format and limits. */
UmicomBootHandoffStatus UmicomKernelBootHandoffRead(const void *bytes,
    UmicomSize accessibleBytes, UmicomKernelBootHandoff *out);
/* Produce the same canonical version-1 bytes accepted by the reader. The
 * complete input object, destination capacity and written counter must be
 * accessible, stable and pairwise nonoverlapping. Input and counter must have
 * their declared alignment. Capacity is 1 through MAX_BYTES; success writes
 * only the encoded record, leaves its destination tail untouched, and updates
 * written. Both outputs are unchanged on every refusal.
 *
 * The writer bounds counts/text before encoding, then calls the existing reader
 * to admit all memory and optional-descriptor rules. Unused input array slots
 * and command bytes after the first zero are not serialized. No physical
 * address is followed; this is not a firmware adapter or new boot entry. */
UmicomBootHandoffStatus UmicomKernelBootHandoffWrite(
    const UmicomKernelBootHandoff *record,void *destination,
    UmicomSize capacity,UmicomSize *written);
#endif
