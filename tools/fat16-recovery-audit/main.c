/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/main.c
 *
 * PURPOSE:
 *   Produce machine-readable FAT16 recovery evidence from a named raw image.
 *   The file is opened read-only. The tool never repairs or mounts the image.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "fat16_recovery_audit.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(_WIN32)
#define UmicomSeek _fseeki64
#define UmicomTell _ftelli64
#else
#define UmicomSeek fseeko
#define UmicomTell ftello
#endif

typedef struct UmicomFat16AuditFile {
    FILE *input;
} UmicomFat16AuditFile;

static bool UmicomReadImageSector(
    void *context,
    uint64_t sectorNumber,
    uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    UmicomFat16AuditFile *source = (UmicomFat16AuditFile *)context;
    const uint64_t maximumOffset = INT64_MAX;
    uint64_t offset;
    if (source == NULL || source->input == NULL
        || sectorNumber > maximumOffset / UMICOM_FAT16_AUDIT_SECTOR_BYTES) {
        return false;
    }
    offset = sectorNumber * UMICOM_FAT16_AUDIT_SECTOR_BYTES;
    if (UmicomSeek(source->input, (int64_t)offset, SEEK_SET) != 0) {
        return false;
    }
    return fread(destination, 1U, UMICOM_FAT16_AUDIT_SECTOR_BYTES, source->input)
           == UMICOM_FAT16_AUDIT_SECTOR_BYTES;
}

/* Refuse character/block devices, pipes and sockets, not just write modes.
 * Operators must explicitly supply a regular disposable raw image. */
static bool UmicomIsRegularImageFile(FILE *file)
{
#if defined(_WIN32)
    struct _stat64 status;
    return _fstat64(_fileno(file), &status) == 0
           && (status.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat status;
    return fstat(fileno(file), &status) == 0 && S_ISREG(status.st_mode);
#endif
}

static bool UmicomParsePartitionIndex(const char *text, uint32_t *result)
{
    char *end = NULL;
    unsigned long value;
    if (text == NULL || text[0] == '\0' || result == NULL || text[0] == '-') {
        return false;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 3UL) {
        return false;
    }
    *result = (uint32_t)value;
    return true;
}

int main(int argc, char **argv)
{
    UmicomFat16AuditSource source = {0};
    UmicomFat16AuditReport report;
    UmicomFat16AuditFile input = {0};
    UmicomFat16AuditClassification classification;
    int64_t imageBytes;
    uint32_t partition = 0U;

    if (argc < 2 || argc > 3 || (argc == 3 && !UmicomParsePartitionIndex(argv[2], &partition))) {
        fputs("Usage: umicom-fat16-recovery-audit IMAGE.raw [primary-partition-0..3]\n", stderr);
        return 64;
    }
    input.input = fopen(argv[1], "rb");
    if (input.input == NULL) {
        perror("Cannot open raw image for read-only audit");
        return 66;
    }
    if (!UmicomIsRegularImageFile(input.input)) {
        fputs("Only regular disposable raw-image files may be inspected\n", stderr);
        (void)fclose(input.input);
        return 65;
    }
    if (UmicomSeek(input.input, 0, SEEK_END) != 0) {
        fputs("Cannot determine image length\n", stderr);
        (void)fclose(input.input);
        return 74;
    }
    imageBytes = (int64_t)UmicomTell(input.input);
    if (imageBytes <= 0 || ((uint64_t)imageBytes % UMICOM_FAT16_AUDIT_SECTOR_BYTES) != 0U) {
        fputs("Image length must be a positive multiple of 512 bytes\n", stderr);
        (void)fclose(input.input);
        return 65;
    }

    source.context = &input;
    source.readSector = UmicomReadImageSector;
    source.mediaSectors = (uint64_t)imageBytes / UMICOM_FAT16_AUDIT_SECTOR_BYTES;
    source.partitionIndex = partition;
    source.readBudget = 0U;
    classification = UmicomFat16AuditInspect(&source, &report);
    if (fclose(input.input) != 0) {
        fputs("Failed to close raw-image reader\n", stderr);
        return 74;
    }

    printf("{\"classification\":\"%s\",\"partition\":%" PRIu32
           ",\"partition_start\":%" PRIu64 ",\"partition_sectors\":%" PRIu64
           ",\"fat_sectors\":%" PRIu32 ",\"clusters\":%" PRIu32
           ",\"sectors_read\":%" PRIu32 ",\"differing_fat_sectors\":%" PRIu32
           ",\"recognised_fat16_geometry\":%s,\"fat_mirrors_match\":%s"
           ",\"clean_flag_clear\":%s,\"io_error_flag_clear\":%s"
           ",\"reserved_fat_entries_valid\":%s,\"repair_performed\":false}\n",
           UmicomFat16AuditClassificationName(classification), partition,
           report.partitionStart, report.partitionSectors, report.fatSectors,
           report.clusterCount, report.sectorsRead, report.differingFatSectors,
           report.recognisedFat16Geometry ? "true" : "false",
           report.fatMirrorsMatch ? "true" : "false",
           report.fatCleanFlagClear ? "true" : "false",
           report.fatIoErrorFlagClear ? "true" : "false",
           report.reservedFatEntriesValid ? "true" : "false");

    /* Only validated, clean, mirrored FAT-header evidence exits zero.
     * Even exit zero does not claim complete filesystem or file-data integrity. */
    if (classification == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED) {
        return 0;
    }
    if (classification == UMICOM_FAT16_AUDIT_INCOMPLETE_READ) {
        return 74;
    }
    return 2;
}
