/*-----------------------------------------------------------------------------
 * Umicom Kernel - read-only guest commit image evidence CLI
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *
 * Two ordinary, read-only raw files: immutable synthetic source and stopped
 * guest's disposable copy. No host disk devices, overwrite or repair entry.
 *---------------------------------------------------------------------------*/
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "fat16_guest_commit_evidence.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#define UmicomGuestSeek _fseeki64
#define UmicomGuestTell _ftelli64
#else
#include <unistd.h>
#define UmicomGuestSeek fseeko
#define UmicomGuestTell ftello
#endif

typedef struct UmicomGuestFile {
    FILE *handle;
} UmicomGuestFile;

static bool UmicomGuestIsRegular(FILE *file)
{
#if defined(_WIN32)
    struct _stat64 info;
    return _fstat64(_fileno(file), &info) == 0 &&
        (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat info;
    return fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode);
#endif
}
static bool UmicomGuestReadFile(void *context, uint64_t sector, uint8_t output[512])
{
    UmicomGuestFile *file = (UmicomGuestFile *)context;
    if (!file || !file->handle || sector > INT64_MAX / UINT64_C(512)) return false;
    if (UmicomGuestSeek(file->handle, (int64_t)(sector * 512U), SEEK_SET) != 0)
        return false;
    return fread(output, 1U, 512U, file->handle) == 512U;
}
static bool UmicomGuestOpenFile(const char *path, UmicomGuestFile *file,
    UmicomFat16AuditSource *source)
{
    file->handle = fopen(path, "rb");
    if (!file->handle) return false;
    if (!UmicomGuestIsRegular(file->handle) || UmicomGuestSeek(file->handle, 0, SEEK_END) != 0)
        return false;
    const int64_t bytes = (int64_t)UmicomGuestTell(file->handle);
    if (bytes <= 0 || bytes % 512 != 0 || bytes / 512 > INT64_C(32768)) return false;
    *source = (UmicomFat16AuditSource){.context = file,
        .readSector = UmicomGuestReadFile, .mediaSectors = (uint64_t)(bytes / 512),
        .partitionIndex = 0U};
    return true;
}

int main(int argc, char **argv)
{
    UmicomGuestFile original = {0}, observed = {0};
    UmicomFat16AuditSource source = {0}, after = {0};
    UmicomFat16GuestEvidenceReport report = {0};
    int result = 2;
    if (argc != 4 || (strcmp(argv[3], "stage") != 0 && strcmp(argv[3], "finish") != 0)) {
        fputs("Usage: umicom-fat16-guest-commit-evidence BEFORE.raw AFTER.raw stage|finish\n"
              "Expected exercise: fatstage /FRAG.BIN 511 \"Umicom ordered update\"\n"
              "Inputs must be regular disposable raw files. No changes are made.\n", stderr);
        return 64;
    }
    if (!UmicomGuestOpenFile(argv[1], &original, &source) ||
        !UmicomGuestOpenFile(argv[2], &observed, &after)) {
        fputs("Could not open both regular raw images read-only, or invalid file size\n", stderr);
        result = 66;
        goto cleanup;
    }
    const UmicomFat16GuestEvidenceMode mode = strcmp(argv[3], "stage") == 0 ?
        UMICOM_FAT16_GUEST_EVIDENCE_STAGED : UMICOM_FAT16_GUEST_EVIDENCE_FINISHED;
    const UmicomFat16GuestEvidenceStatus status =
        UmicomFat16GuestEvidenceInspect(&source, &after, mode, &report);
    printf("{\"classification\":\"%s\",\"mode\":\"%s\""
           ",\"before_header\":\"%s\",\"after_header\":\"%s\""
           ",\"before_allocation\":\"%s\",\"after_allocation\":\"%s\""
           ",\"scanned_sectors\":%" PRIu64 ",\"expected_changed_bytes\":%" PRIu64
           ",\"actual_changed_bytes\":%" PRIu64 ",\"unexpected_bytes\":%" PRIu64
           ",\"first_unexpected_sector\":%" PRIu64 ",\"first_unexpected_offset\":%" PRIu32
           ",\"expected_byte\":%u,\"actual_byte\":%u"
           ",\"first_data_sector\":%" PRIu64 ",\"second_data_sector\":%" PRIu64
           ",\"full_image_compared\":%s,\"repair_performed\":false}\n",
           UmicomFat16GuestEvidenceStatusName(status), argv[3],
           UmicomFat16AuditClassificationName(report.beforeHeader),
           UmicomFat16AuditClassificationName(report.afterHeader),
           UmicomFat16IntegrityClassificationName(report.beforeIntegrity),
           UmicomFat16IntegrityClassificationName(report.afterIntegrity),
           report.mediaSectorsCompared, report.expectedDifferenceBytes,
           report.actualDifferenceBytes, report.unexpectedBytes,
           report.firstUnexpectedSector, report.firstUnexpectedOffset,
           (unsigned)report.firstExpected, (unsigned)report.firstActual,
           report.firstDataSector, report.secondDataSector,
           report.fullImageCompared ? "true" : "false");
    if (status == UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED) result = 0;
    else if (status == UMICOM_FAT16_GUEST_EVIDENCE_READ_FAILED) result = 74;
    else if (status == UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT) result = 64;
cleanup:
    if (original.handle && fclose(original.handle) != 0) result = 74;
    if (observed.handle && fclose(observed.handle) != 0) result = 74;
    return result;
}
