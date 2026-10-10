/*-----------------------------------------------------------------------------
 * Umicom Kernel: independently generated complete-image commit evidence
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *
 * The generator mirrors the documented public synthetic MBR/FAT16 fixture,
 * not the production updater or verifier's expected-patch mapping routine.
 * Both the positive and negative cases use the real auditors and compare ALL
 * image sectors. --fixtures is CTest-owned disposable output only.
 *---------------------------------------------------------------------------*/
#include "fat16_guest_commit_evidence.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TEST_SECTORS UINT32_C(16384)
#define TEST_IMAGE_BYTES ((size_t)TEST_SECTORS * 512U)
#define PART_START UINT32_C(2048)
#define FAT_SECTORS UINT32_C(48)
#define ROOT_SECTOR (PART_START + 97U)
#define DATA_SECTOR (PART_START + 129U)

static void Put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}
static void Put32(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0U; i < 4U; ++i) p[i] = (uint8_t)(value >> (i * 8U));
}
static uint8_t *Sector(uint8_t *image, uint32_t sector)
{
    return image + (size_t)sector * 512U;
}
static void Entry(uint8_t *where, const char name[11], uint8_t attr,
    uint16_t cluster, uint32_t bytes)
{
    memcpy(where, name, 11U);
    where[11] = attr;
    Put16(where + 26U, cluster);
    Put32(where + 28U, bytes);
}
static void BuildOriginal(uint8_t *image)
{
    memset(image, 0, TEST_IMAGE_BYTES);
    uint8_t *mbr = Sector(image, 0U);
    uint8_t *part = mbr + 446U;
    part[1] = 0xfeU; part[2] = 0xffU; part[3] = 0xffU;
    part[4] = 0x06U; part[5] = 0xfeU; part[6] = 0xffU; part[7] = 0xffU;
    Put32(part + 8U, PART_START); Put32(part + 12U, 12288U);
    mbr[510] = 0x55U; mbr[511] = 0xaaU;
    uint8_t *boot = Sector(image, PART_START);
    boot[0] = 0xebU; boot[1] = 0x3cU; boot[2] = 0x90U;
    memcpy(boot + 3U, "UMICOM  ", 8U);
    Put16(boot + 11U, 512U); boot[13] = 1U;
    Put16(boot + 14U, 1U); boot[16] = 2U;
    Put16(boot + 17U, 512U); Put16(boot + 19U, 12288U);
    boot[21] = 0xf8U; Put16(boot + 22U, FAT_SECTORS);
    Put16(boot + 24U, 63U); Put16(boot + 26U, 255U);
    Put32(boot + 28U, PART_START);
    boot[36] = 0x80U; boot[38] = 0x29U;
    Put32(boot + 39U, UINT32_C(0x554d4346));
    memcpy(boot + 43U, "UMICOMDISK ", 11U);
    memcpy(boot + 54U, "FAT16   ", 8U);
    boot[510] = 0x55U; boot[511] = 0xaaU;
    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        uint8_t *fat = Sector(image, PART_START + 1U + copy * FAT_SECTORS);
        Put16(fat, UINT16_C(0xfff8)); Put16(fat + 2U, UINT16_C(0xffff));
        Put16(fat + 4U, UINT16_C(0xffff)); /* README */
        Put16(fat + 6U, UINT16_C(0xffff)); /* DOCS */
        Put16(fat + 8U, 9U);             /* first FRAG cluster -> 9 */
        Put16(fat + 12U, UINT16_C(0xffff)); /* third FRAG */
        Put16(fat + 14U, UINT16_C(0xffff)); /* GUIDE */
        Put16(fat + 18U, 6U);           /* second FRAG -> 6 */
    }
    uint8_t *root = Sector(image, ROOT_SECTOR);
    Entry(root, "UMICOMDISK ", 0x08U, 0U, 0U);
    static const char readme[] =
        "Umicom Kernel read-only FAT16 fixture.\n"
        "These files are synthetic test data, not a host disk.\n"
        "No disk writes or repairs are enabled.\n";
    static const char guide[] =
        "Partition offsets are checked before filesystem reads.\n"
        "FRAG.BIN deliberately uses non-contiguous clusters.\n";
    Entry(root + 32U, "README  TXT", 0x21U, 2U, sizeof(readme) - 1U);
    Entry(root + 64U, "DOCS       ", 0x10U, 3U, 0U);
    Entry(root + 96U, "FRAG    BIN", 0x20U, 4U, 1300U);
    Entry(root + 128U, "EMPTY   TXT", 0x20U, 0U, 0U);
    memcpy(Sector(image, DATA_SECTOR), readme, sizeof(readme) - 1U);
    uint8_t *docs = Sector(image, DATA_SECTOR + 1U);
    Entry(docs, ".          ", 0x10U, 3U, 0U);
    Entry(docs + 32U, "..         ", 0x10U, 0U, 0U);
    Entry(docs + 64U, "GUIDE   TXT", 0x21U, 7U, sizeof(guide) - 1U);
    memcpy(Sector(image, DATA_SECTOR + 5U), guide, sizeof(guide) - 1U);
    const uint32_t fragSectors[3] = {DATA_SECTOR + 2U, DATA_SECTOR + 7U,
        DATA_SECTOR + 4U};
    for (uint32_t cluster = 0U; cluster < 3U; ++cluster) {
        uint8_t *data = Sector(image, fragSectors[cluster]);
        for (uint32_t j = 0U; j < 512U; ++j) {
            const uint32_t fileOffset = cluster * 512U + j;
            data[j] = fileOffset < 1300U ?
                (uint8_t)((fileOffset * 37U + 11U) & 255U) : 0xa6U;
        }
    }
}
static void ApplyPatch(uint8_t *image, bool stage)
{
    static const char patch[] = "Umicom ordered update";
    Sector(image, DATA_SECTOR + 2U)[511] = (uint8_t)patch[0];
    memcpy(Sector(image, DATA_SECTOR + 7U), patch + 1U, sizeof(patch) - 2U);
    if (stage) {
        Sector(image, PART_START + 1U)[3] = 0x7fU;
        Sector(image, PART_START + 1U + FAT_SECTORS)[3] = 0x7fU;
    }
}

typedef struct MemorySource {
    const uint8_t *data;
    uint64_t refusedSector;
} MemorySource;
static bool ReadMemory(void *context, uint64_t sector, uint8_t destination[512])
{
    MemorySource *m = (MemorySource *)context;
    if (!m || !m->data || sector >= TEST_SECTORS || sector == m->refusedSector) return false;
    memcpy(destination, m->data + (size_t)sector * 512U, 512U);
    return true;
}
static UmicomFat16GuestEvidenceStatus Check(const uint8_t *before,
    const uint8_t *after, UmicomFat16GuestEvidenceMode mode,
    UmicomFat16GuestEvidenceReport *report)
{
    MemorySource a = {before, UINT64_MAX};
    MemorySource b = {after, UINT64_MAX};
    UmicomFat16AuditSource original = {&a, ReadMemory, TEST_SECTORS, 0U, 0U};
    UmicomFat16AuditSource modified = {&b, ReadMemory, TEST_SECTORS, 0U, 0U};
    return UmicomFat16GuestEvidenceInspect(&original, &modified, mode, report);
}
static bool WriteImage(const char *path, const uint8_t *bytes)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes, 1U, TEST_IMAGE_BYTES, f) == TEST_IMAGE_BYTES;
    return fclose(f) == 0 && ok;
}
static uint32_t checks = 0U;
static uint32_t failures = 0U;
static void Expect(bool condition, const char *label)
{
    ++checks;
    if (!condition) { ++failures; fprintf(stderr, "FAIL %s\n", label); }
}
int main(int argc, char **argv)
{
    uint8_t *original = (uint8_t *)malloc(TEST_IMAGE_BYTES);
    uint8_t *staged = (uint8_t *)malloc(TEST_IMAGE_BYTES);
    uint8_t *finished = (uint8_t *)malloc(TEST_IMAGE_BYTES);
    if (!original || !staged || !finished) {
        fputs("Cannot allocate finite synthetic image buffers\n", stderr);
        free(original); free(staged); free(finished);
        return 1;
    }
    BuildOriginal(original);
    memcpy(staged, original, TEST_IMAGE_BYTES);
    memcpy(finished, original, TEST_IMAGE_BYTES);
    ApplyPatch(staged, true);
    ApplyPatch(finished, false);
    if (argc == 6 && strcmp(argv[1], "--fixtures") == 0) {
        bool ok = WriteImage(argv[2], original) && WriteImage(argv[3], staged)
            && WriteImage(argv[4], finished);
        Sector(finished, DATA_SECTOR + 7U)[21] ^= 1U; /* non-target neighbour */
        ok = WriteImage(argv[5], finished) && ok;
        printf("fixture-generation=%s\n", ok ? "passed" : "failed");
        free(original); free(staged); free(finished);
        return ok ? 0 : 1;
    }
    if (argc != 1) {
        fputs("Usage: tests [--fixtures ORIGINAL STAGED FINISHED CORRUPT]\n", stderr);
        free(original); free(staged); free(finished);
        return 64;
    }
    UmicomFat16GuestEvidenceReport report = {0};
    Expect(Check(original, staged, UMICOM_FAT16_GUEST_EVIDENCE_STAGED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED, "staged full-image verification");
    Expect(report.fullImageCompared && report.unexpectedBytes == 0U &&
        report.mediaSectorsCompared == TEST_SECTORS, "staged all sectors read");
    Expect(report.firstDataSector == DATA_SECTOR + 2U &&
        report.secondDataSector == DATA_SECTOR + 7U, "fragmented mapping derived");
    Expect(report.afterHeader == UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
        "staged media explicitly dirty");
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED, "finished full-image verification");
    Expect(report.afterIntegrity == UMICOM_FAT16_INTEGRITY_CONSISTENT,
        "finished allocation accepted");
    Expect(Check(original, original, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER,
        "untouched disk cannot falsely pass an expected patch");
    Expect(Check(original, staged, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED,
        "dirty disk rejected as finished");
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_STAGED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED,
        "clean disk rejected as staged");
    Sector(finished, DATA_SECTOR + 7U)[21] ^= 1U;
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER,
        "neighbour byte tampering refused");
    Expect(report.unexpectedBytes == 1U && report.firstUnexpectedSector == DATA_SECTOR + 7U,
        "unexpected byte precise location");
    Sector(finished, DATA_SECTOR + 7U)[21] ^= 1U;
    Sector(finished, ROOT_SECTOR)[200] = 42U;
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER,
        "unrelated root byte modification refused");
    Sector(finished, ROOT_SECTOR)[200] = 0U;
    Sector(staged, PART_START + 1U)[3] = 0xffU;
    Expect(Check(original, staged, UMICOM_FAT16_GUEST_EVIDENCE_STAGED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED,
        "one dirty guard is not enough");
    Sector(staged, PART_START + 1U)[3] = 0x7fU;
    Sector(staged, DATA_SECTOR + 2U)[511] ^= 1U;
    Expect(Check(original, staged, UMICOM_FAT16_GUEST_EVIDENCE_STAGED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER,
        "incorrect payload refused");
    Sector(staged, DATA_SECTOR + 2U)[511] ^= 1U;
    Sector(original, PART_START + 1U)[3] = 0x7fU;
    Sector(original, PART_START + 1U + FAT_SECTORS)[3] = 0x7fU;
    Expect(Check(original, staged, UMICOM_FAT16_GUEST_EVIDENCE_STAGED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED,
        "dirty initial image rejected");
    Sector(original, PART_START + 1U)[3] = 0xffU;
    Sector(original, PART_START + 1U + FAT_SECTORS)[3] = 0xffU;
    Sector(original, ROOT_SECTOR)[96] = 'X';
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_FORMAT_REFUSED,
        "missing FRAG alias refused");
    Sector(original, ROOT_SECTOR)[96] = 'F';
    Sector(original, PART_START + 1U)[8] = 0xffU;
    Sector(original, PART_START + 1U + FAT_SECTORS)[8] = 0xffU;
    Expect(Check(original, finished, UMICOM_FAT16_GUEST_EVIDENCE_FINISHED,
        &report) == UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED,
        "truncated allocation chain refused at source");
    Sector(original, PART_START + 1U)[8] = 9U;
    Sector(original, PART_START + 1U + FAT_SECTORS)[8] = 9U;
    MemorySource a = {original, UINT64_MAX}, b = {finished, UINT64_MAX};
    UmicomFat16AuditSource before = {&a, ReadMemory, TEST_SECTORS, 0U, 0U};
    UmicomFat16AuditSource after = {&b, ReadMemory, TEST_SECTORS, 0U, 0U};
    Expect(UmicomFat16GuestEvidenceInspect(NULL, &after,
        UMICOM_FAT16_GUEST_EVIDENCE_FINISHED, &report) ==
        UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT, "missing source rejected");
    Expect(UmicomFat16GuestEvidenceInspect(&before, &after,
        (UmicomFat16GuestEvidenceMode)99, &report) ==
        UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT, "bad mode rejected");
    a.refusedSector = 0U;
    Expect(UmicomFat16GuestEvidenceInspect(&before, &after,
        UMICOM_FAT16_GUEST_EVIDENCE_FINISHED, &report) ==
        UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED, "source read error refused");
    a.refusedSector = UINT64_MAX;
    b.refusedSector = DATA_SECTOR + 7U;
    Expect(UmicomFat16GuestEvidenceInspect(&before, &after,
        UMICOM_FAT16_GUEST_EVIDENCE_FINISHED, &report) ==
        UMICOM_FAT16_GUEST_EVIDENCE_READ_FAILED, "after late data sector read fails closed");
    b.refusedSector = UINT64_MAX;
    after.mediaSectors = TEST_SECTORS - 1U;
    Expect(UmicomFat16GuestEvidenceInspect(&before, &after,
        UMICOM_FAT16_GUEST_EVIDENCE_FINISHED, &report) ==
        UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT, "different sized disks rejected");
    Expect(UmicomFat16GuestEvidenceInspect(&before, &after,
        UMICOM_FAT16_GUEST_EVIDENCE_FINISHED, NULL) ==
        UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT, "null result rejected");
    printf("guest-commit-evidence-tests=%u passed=%u failed=%u\n",
        checks, checks - failures, failures);
    free(original); free(staged); free(finished);
    return failures == 0U ? 0 : 1;
}
