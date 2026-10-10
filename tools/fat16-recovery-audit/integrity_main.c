/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/integrity_main.c
 *
 * PURPOSE:
 *   A separate, read-only host executable for FAT16 namespace/allocation
 *   consistency evidence, leaving the existing header audit CLI unchanged.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "fat16_integrity_audit.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#define UmicomIntegritySeek _fseeki64
#define UmicomIntegrityTell _ftelli64
#else
#include <unistd.h>
#define UmicomIntegritySeek fseeko
#define UmicomIntegrityTell ftello
#endif

typedef struct UmicomIntegrityFile { FILE *input; } UmicomIntegrityFile;

static bool UmicomIntegrityReadFileSector(void *context, uint64_t index,
    uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    const UmicomIntegrityFile *file = (const UmicomIntegrityFile *)context;
    if (file == NULL || file->input == NULL
        || index > (uint64_t)INT64_MAX / UMICOM_FAT16_AUDIT_SECTOR_BYTES) {
        return false;
    }
    return UmicomIntegritySeek(file->input,
               (int64_t)(index * UMICOM_FAT16_AUDIT_SECTOR_BYTES), SEEK_SET) == 0
           && fread(destination, 1U, UMICOM_FAT16_AUDIT_SECTOR_BYTES, file->input)
                  == UMICOM_FAT16_AUDIT_SECTOR_BYTES;
}

static bool UmicomIntegrityRegularFile(FILE *file)
{
#if defined(_WIN32)
    struct _stat64 value;
    return _fstat64(_fileno(file), &value) == 0
           && (value.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat value;
    return fstat(fileno(file), &value) == 0 && S_ISREG(value.st_mode);
#endif
}

static bool UmicomIntegrityPartition(const char *text, uint32_t *partition)
{
    char *end = NULL;
    unsigned long value;
    if (text == NULL || text[0] == '\0' || text[0] == '-' || partition == NULL) {
        return false;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 3UL) {
        return false;
    }
    *partition = (uint32_t)value;
    return true;
}

int main(int argc, char **argv)
{
    UmicomIntegrityFile file = {0};
    UmicomFat16AuditSource source = {0};
    UmicomFat16IntegrityReport report = {0};
    UmicomFat16IntegrityClassification classification;
    int64_t imageLength;
    uint32_t partition = 0U;
    if (argc < 2 || argc > 3 || (argc == 3 && !UmicomIntegrityPartition(argv[2], &partition))) {
        fputs("Usage: umicom-fat16-integrity-audit IMAGE.raw [primary-partition-0..3]\n", stderr);
        return 64;
    }
    file.input = fopen(argv[1], "rb");
    if (file.input == NULL) {
        perror("Unable to open raw image read-only");
        return 66;
    }
    if (!UmicomIntegrityRegularFile(file.input)) {
        fputs("Refusing non-regular image; use a disposable raw-image copy\n", stderr);
        (void)fclose(file.input);
        return 65;
    }
    if (UmicomIntegritySeek(file.input, 0, SEEK_END) != 0
        || (imageLength = (int64_t)UmicomIntegrityTell(file.input)) <= 0
        || ((uint64_t)imageLength % UMICOM_FAT16_AUDIT_SECTOR_BYTES) != 0U) {
        fputs("Image size unavailable or not a positive multiple of 512 bytes\n", stderr);
        (void)fclose(file.input);
        return 65;
    }
    source.context = &file;
    source.readSector = UmicomIntegrityReadFileSector;
    source.mediaSectors = (uint64_t)imageLength / UMICOM_FAT16_AUDIT_SECTOR_BYTES;
    source.partitionIndex = partition;
    classification = UmicomFat16IntegrityInspect(&source, NULL, &report);
    if (fclose(file.input) != 0) {
        fputs("Failed to close audit input\n", stderr);
        return 74;
    }
    /* JSON values are measured, bounded integers and constant classifications;
     * no filename or untrusted on-disk string enters the JSON output. */
    printf("{\"classification\":\"%s\",\"header_classification\":\"%s\""
           ",\"partition\":%" PRIu32 ",\"header_reads\":%" PRIu32
           ",\"additional_reads\":%" PRIu32
           ",\"directories\":%" PRIu32 ",\"files\":%" PRIu32
           ",\"entries\":%" PRIu32 ",\"allocated_clusters\":%" PRIu32
           ",\"referenced_clusters\":%" PRIu32 ",\"orphan_clusters\":%" PRIu32
           ",\"crosslinks\":%" PRIu32 ",\"loops\":%" PRIu32
           ",\"invalid_chains\":%" PRIu32 ",\"short_chains\":%" PRIu32
           ",\"invalid_entries\":%" PRIu32 ",\"bad_cluster_markers\":%" PRIu32
           ",\"fat_copies_stable\":%s,\"namespace_scanned\":%s,\"repair_performed\":false}\n",
           UmicomFat16IntegrityClassificationName(classification),
           UmicomFat16AuditClassificationName(report.header.classification),
           partition, report.header.sectorsRead, report.additionalSectorsRead,
           report.directoriesVisited, report.filesVisited,
           report.directoryEntriesVisited, report.allocatedClusters,
           report.referencedClusters, report.orphanClusters,
           report.crossLinkedClusters, report.loopedChains,
           report.invalidChains, report.truncatedChains,
           report.invalidEntries, report.badClusterMarkers,
           report.fatCopiesStable ? "true" : "false",
           report.scannedNamespace ? "true" : "false");
    if (classification == UMICOM_FAT16_INTEGRITY_CONSISTENT) return 0;
    if (classification == UMICOM_FAT16_INTEGRITY_IO_FAILURE) return 74;
    if (classification == UMICOM_FAT16_INTEGRITY_LIMIT
        || classification == UMICOM_FAT16_INTEGRITY_NO_MEMORY) return 75;
    if (classification == UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT) return 64;
    return 2;
}
