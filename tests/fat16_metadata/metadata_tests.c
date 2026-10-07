/*-----------------------------------------------------------------------------
 * Umicom Kernel — persisted FAT16 metadata qualification.
 *
 * The unchanged inspector fixture helpers provide real media bytes. This
 * suite tests the new metadata snapshot and timestamp decoder against those
 * bytes, independent calendar enumerations and every failed read position.
 * Read callbacks have no write/flush/reset operation; full-image comparisons
 * establish that inspection never edits the immutable synthetic medium.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#define main UmicomMetadataOriginalInspectorEntry
#include "../disk_inspection/disk_inspection_tests.c"
#undef main
#include "umicom/kernel/fat16_metadata.h"

#define METADATA_MEDIA_BYTES ((UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U)
static UmicomU8 metadataMedia[METADATA_MEDIA_BYTES], metadataBefore[METADATA_MEDIA_BYTES];
static UmicomKernelFat16Metadata metadata, metadataPrevious;
static char metadataPath[UMICOM_FAT16_PATH_BYTES];
static UmicomBoolean metadataReenter, metadataChangePath;
static UmicomSize metadataReentries;
static UmicomU16 metadataTime = 0x747dU, metadataDate = 0x7377U;
static UmicomSize metadataCorruptRead, metadataCorruptOffset;
static UmicomU8 metadataCorruptMask;

static void MetadataFilled(const void *storage, UmicomSize bytes, UmicomU8 expected)
{
    const UmicomU8 *const data = storage;
    for (UmicomSize i = 0U; i < bytes; ++i) CHECK(data[i] == expected);
}
static UmicomBoolean MetadataReadSector(void *context, UmicomU64 sector, UmicomU8 *target)
{
    if (metadataReenter) {
        UmicomKernelFat16Metadata ignored, before;
        memset(&ignored, 0x79, sizeof(ignored)); memcpy(&before, &ignored, sizeof(before));
        CHECK(UmicomKernelFat16MetadataRead(&volume, "/README.TXT", &ignored) == UMICOM_DISK_BUSY);
        CHECK(!memcmp(&ignored, &before, sizeof(ignored)));
        CHECK(UmicomKernelFat16Close(&volume) == UMICOM_DISK_BUSY);
        ++metadataReentries;
    }
    if (metadataChangePath) {
        metadataChangePath = UMICOM_FALSE;
        memcpy(metadataPath, "/README.TXT", 12U);
    }
    const UmicomBoolean complete = Read(context, sector, target);
    if (complete && metadataCorruptRead == reads) {
        CHECK(metadataCorruptOffset < 512U); target[metadataCorruptOffset] ^= metadataCorruptMask;
    }
    return complete;
}
static void MetadataStart(const char *fixture)
{
    image = metadataMedia;
    FILE *file = fopen(fixture, "rb"); CHECK(file);
    CHECK(fread(image, 1U, (size_t)METADATA_MEDIA_BYTES, file) == (size_t)METADATA_MEDIA_BYTES);
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    memset(&volume, 0, sizeof(volume)); memset(&reader, 0, sizeof(reader));
    reader.sectors = UMICOM_DISK_FIXTURE_SECTORS; reader.read = MetadataReadSector; reader.context = &reader;
    reads = 0U; failRead = 0U; outside = 0U; reenter = UMICOM_FALSE;
    metadataReenter = UMICOM_FALSE; metadataChangePath = UMICOM_FALSE; metadataReentries = 0U;
    metadataCorruptRead = 0U; metadataCorruptOffset = 0U;
    metadataCorruptMask = 0x53U;
    memset(metadataPath, 0, sizeof(metadataPath)); memcpy(metadataPath, "/FRAG.BIN", 10U);
    metadataTime = 0x747dU; metadataDate = 0x7377U;
    Put16(Root() + 3U * 32U + 22U, metadataTime); Put16(Root() + 3U * 32U + 24U, metadataDate);
    memcpy(metadataBefore, image, sizeof(metadataBefore));
    memset(&metadata, 0xa5, sizeof(metadata)); memcpy(&metadataPrevious, &metadata, sizeof(metadata));
}
static void MetadataRebase(void) { memcpy(metadataBefore, image, sizeof(metadataBefore)); }
static void MetadataUnchanged(void)
{
    CHECK(!memcmp(metadataBefore, image, sizeof(metadataBefore)) && !outside);
}
static void MetadataExpectedTime(const UmicomKernelFat16Timestamp *timestamp)
{
    CHECK(timestamp->rawTime == metadataTime && timestamp->rawDate == metadataDate);
    CHECK(timestamp->state == UMICOM_FAT16_TIMESTAMP_VALID);
    CHECK(timestamp->value.year == 2037U && timestamp->value.month == 11U && timestamp->value.day == 23U);
    CHECK(timestamp->value.hour == 14U && timestamp->value.minute == 35U && timestamp->value.second == 58U);
}
static void MetadataSuccess(const char *name)
{
    const char *path = metadataPath;
    if (!strcmp(name, "archive_clear")) Root()[3U * 32U + 11U] = 0U;
    else if (!strcmp(name, "hidden_system")) Root()[3U * 32U + 11U] = 0x27U;
    else if (!strcmp(name, "casefold")) path = "/frag.bin";
    else if (!strcmp(name, "root_later_sector") || !strcmp(name, "root_last_entry")) {
        const unsigned slot = !strcmp(name, "root_later_sector") ? 19U : 511U;
        UmicomU8 entry[32]; memcpy(entry, Root() + 96U, sizeof(entry)); Root()[96U] = 0xe5U;
        for (unsigned i = 5U; i < slot; ++i) { memset(Root() + i * 32U, 0x79, 32U); Root()[i * 32U] = 0xe5U; }
        memcpy(Root() + slot * 32U, entry, sizeof(entry));
    } else if (!strcmp(name, "nested") || !strcmp(name, "fragmented_parent") || !strcmp(name, "nested_last_entry")) {
        UmicomU8 entry[32]; memcpy(entry, Root() + 96U, sizeof(entry)); Root()[96U] = 0xe5U;
        unsigned cluster = 3U, slot = 3U;
        if (strcmp(name, "nested")) {
            Link(3U, 34U); Link(34U, 67U); Link(67U, 0xffffU);
            for (unsigned i = 3U; i < 16U; ++i) Data(3U)[i * 32U] = 0xe5U;
            memset(Data(34U), 0x63, 512U); for (unsigned i = 0U; i < 16U; ++i) Data(34U)[i * 32U] = 0xe5U;
            cluster = 67U; slot = !strcmp(name, "nested_last_entry") ? 15U : 7U;
            memset(Data(cluster), 0, 512U);
            for (unsigned i = 0U; i < slot; ++i) { memset(Data(cluster) + i * 32U, 0x67, 32U); Data(cluster)[i * 32U] = 0xe5U; }
        }
        memcpy(Data(cluster) + slot * 32U, entry, sizeof(entry)); path = "/DOCS/FRAG.BIN";
    } else if (!strcmp(name, "long_names_skipped")) {
        memset(Root() + 160U, 0x41, 32U); Root()[171U] = 0x0fU;
    } else if (!strcmp(name, "deleted_records")) {
        memset(Root() + 160U, 0x93, 32U); Root()[160U] = 0xe5U;
    } else if (!strcmp(name, "orphan_allocation")) Link(20U, 0xffffU);
    else if (!strcmp(name, "target_cycle")) Link(6U, 4U);
    else if (!strcmp(name, "target_short_chain")) Link(4U, 0xffffU);
    else if (!strcmp(name, "creation_access_ignored")) {
        memset(Root() + 96U + 13U, 0xff, 7U);
    } else if (!strcmp(name, "path_snapshot")) metadataChangePath = UMICOM_TRUE;
    else if (!strcmp(name, "callback_reentry")) metadataReenter = UMICOM_TRUE;
    else CHECK(!strcmp(name, "ordinary") || !strcmp(name, "owner_reuse"));
    MetadataRebase();
    const UmicomBoolean change = metadataChangePath, callback = metadataReenter;
    metadataChangePath = UMICOM_FALSE; metadataReenter = UMICOM_FALSE; Open();
    metadataChangePath = change; metadataReenter = callback;
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_OK);
    CHECK(metadata.directoryEntryPresent && !metadata.entry.directory && !strcmp(metadata.entry.name, "FRAG.BIN"));
    CHECK(metadata.entry.bytes == 1300U && metadata.entry.firstCluster == 4U);
    CHECK(metadata.entry.attributes == (!strcmp(name, "archive_clear") ? 0U : (!strcmp(name, "hidden_system") ? 0x27U : 0x20U)));
    MetadataExpectedTime(&metadata.writeTimestamp);
    CHECK(!volume.busy); MetadataUnchanged();
    if (change) CHECK(!strcmp(metadataPath, "/README.TXT"));
    if (callback) CHECK(metadataReentries > 0U);
    metadataReenter = UMICOM_FALSE;
    if (!strcmp(name, "owner_reuse")) {
        UmicomKernelFat16Metadata snapshot = metadata;
        CHECK(UmicomKernelFat16MetadataRead(&volume, "/README.TXT", &metadata) == UMICOM_DISK_OK);
        CHECK(!strcmp(metadata.entry.name, "README.TXT") && metadata.writeTimestamp.state == UMICOM_FAT16_TIMESTAMP_ABSENT);
        CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_OK);
        CHECK(!memcmp(&snapshot, &metadata, sizeof(metadata)));
    }
    Close(); MetadataUnchanged();
}
static void MetadataObject(const char *name)
{
    const char *path = "/";
    if (!strcmp(name, "directory")) {
        Put16(Root() + 64U + 22U, metadataTime); Put16(Root() + 64U + 24U, metadataDate); path = "/DOCS";
    } else if (!strcmp(name, "empty_file")) {
        Put16(Root() + 128U + 22U, metadataTime); Put16(Root() + 128U + 24U, metadataDate); path = "/EMPTY.TXT";
    } else if (!strcmp(name, "nested_readonly")) {
        Put16(Data(3U) + 64U + 22U, metadataTime); Put16(Data(3U) + 64U + 24U, metadataDate); path = "/DOCS/GUIDE.TXT";
    } else CHECK(!strcmp(name, "root"));
    MetadataRebase(); Open(); const UmicomSize before = reads;
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_OK);
    if (!strcmp(name, "root")) {
        CHECK(reads == before && !metadata.directoryEntryPresent);
        CHECK(!strcmp(metadata.entry.name, "/") && metadata.entry.directory && !metadata.entry.firstCluster && !metadata.entry.bytes);
        CHECK(metadata.writeTimestamp.state == UMICOM_FAT16_TIMESTAMP_ABSENT);
        CHECK(!metadata.writeTimestamp.rawTime && !metadata.writeTimestamp.rawDate);
        MetadataFilled(&metadata.writeTimestamp.value, sizeof(metadata.writeTimestamp.value), 0U);
    } else {
        CHECK(metadata.directoryEntryPresent); MetadataExpectedTime(&metadata.writeTimestamp);
        CHECK(metadata.entry.directory == (!strcmp(name, "directory") ? UMICOM_TRUE : UMICOM_FALSE));
        if (!strcmp(name, "empty_file")) CHECK(!metadata.entry.bytes && !metadata.entry.firstCluster);
        if (!strcmp(name, "nested_readonly")) CHECK(metadata.entry.attributes & 1U);
    }
    Close(); MetadataUnchanged();
}
static void MetadataRaw(const char *name)
{
    UmicomKernelFat16TimestampState expected = UMICOM_FAT16_TIMESTAMP_INVALID;
    metadataTime = 0U; metadataDate = 0U;
    if (!strcmp(name, "absent")) expected = UMICOM_FAT16_TIMESTAMP_ABSENT;
    else if (!strcmp(name, "midnight")) { metadataDate = 0x0021U; expected = UMICOM_FAT16_TIMESTAMP_VALID; }
    else if (!strcmp(name, "date_missing")) metadataTime = 0x747dU;
    else if (!strcmp(name, "month_zero")) metadataDate = 0x7217U;
    else if (!strcmp(name, "month_high")) metadataDate = 0x73b7U;
    else if (!strcmp(name, "day_zero")) metadataDate = 0x7360U;
    else if (!strcmp(name, "april_31")) metadataDate = 0x729fU;
    else if (!strcmp(name, "century_nonleap")) metadataDate = 0xf05dU;
    else if (!strcmp(name, "hour_24")) { metadataTime = 0xc000U; metadataDate = 0x7377U; }
    else if (!strcmp(name, "minute_60")) { metadataTime = 0x0780U; metadataDate = 0x7377U; }
    else if (!strcmp(name, "second_60")) { metadataTime = 30U; metadataDate = 0x7377U; }
    else if (!strcmp(name, "second_62")) { metadataTime = 31U; metadataDate = 0x7377U; }
    else CHECK(0);
    Put16(Root() + 96U + 22U, metadataTime); Put16(Root() + 96U + 24U, metadataDate); MetadataRebase();
    Open(); CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_OK);
    CHECK(metadata.directoryEntryPresent && metadata.writeTimestamp.state == expected);
    CHECK(metadata.writeTimestamp.rawTime == metadataTime && metadata.writeTimestamp.rawDate == metadataDate);
    if (expected == UMICOM_FAT16_TIMESTAMP_VALID) {
        CHECK(metadata.writeTimestamp.value.year == 1980U && metadata.writeTimestamp.value.month == 1U && metadata.writeTimestamp.value.day == 1U);
        CHECK(!metadata.writeTimestamp.value.hour && !metadata.writeTimestamp.value.minute && !metadata.writeTimestamp.value.second);
    } else MetadataFilled(&metadata.writeTimestamp.value, sizeof(metadata.writeTimestamp.value), 0U);
    Close(); MetadataUnchanged();
}

static void MetadataDecode(const char *name)
{
    static UmicomKernelFat16FileTime calendar[65536];
    static UmicomBoolean valid[65536];
    UmicomKernelFat16Timestamp decoded;
    if (!strcmp(name, "all_dates")) {
        static const unsigned monthDays[] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
        for (unsigned year = 1980U; year <= 2107U; ++year) {
            for (unsigned month = 1U; month <= 12U; ++month) {
                const unsigned days = monthDays[month - 1U] +
                    (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U) ? 1U : 0U);
                for (unsigned day = 1U; day <= days; ++day) {
                    const unsigned word = (year - 1980U) * 512U + month * 32U + day;
                    valid[word] = UMICOM_TRUE;
                    calendar[word] = (UmicomKernelFat16FileTime){(UmicomU16)year,(UmicomU16)month,(UmicomU16)day,0U,0U,0U};
                }
            }
        }
        for (unsigned word = 0U; word <= 65535U; ++word) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(UmicomKernelFat16TimestampDecode(0U, (UmicomU16)word, &decoded) == UMICOM_DISK_OK);
            CHECK(decoded.rawTime == 0U && decoded.rawDate == word);
            CHECK(decoded.state == (valid[word] ? UMICOM_FAT16_TIMESTAMP_VALID :
                (word ? UMICOM_FAT16_TIMESTAMP_INVALID : UMICOM_FAT16_TIMESTAMP_ABSENT)));
            CHECK(!memcmp(&decoded.value, &calendar[word], sizeof(decoded.value)));
        }
    } else if (!strcmp(name, "all_times")) {
        for (unsigned hour = 0U; hour < 24U; ++hour)
            for (unsigned minute = 0U; minute < 60U; ++minute)
                for (unsigned second = 0U; second < 60U; second += 2U) {
                    const unsigned word = hour * 2048U + minute * 32U + second / 2U;
                    valid[word] = UMICOM_TRUE;
                    calendar[word] = (UmicomKernelFat16FileTime){2000U,2U,29U,(UmicomU16)hour,(UmicomU16)minute,(UmicomU16)second};
                }
        for (unsigned word = 0U; word <= 65535U; ++word) {
            memset(&decoded, 0xa5, sizeof(decoded));
            CHECK(UmicomKernelFat16TimestampDecode((UmicomU16)word, 0x285dU, &decoded) == UMICOM_DISK_OK);
            CHECK(decoded.rawTime == word && decoded.rawDate == 0x285dU);
            CHECK(decoded.state == (valid[word] ? UMICOM_FAT16_TIMESTAMP_VALID : UMICOM_FAT16_TIMESTAMP_INVALID));
            CHECK(!memcmp(&decoded.value, &calendar[word], sizeof(decoded.value)));
        }
    } else {
        _Alignas(UmicomKernelFat16Timestamp) UmicomU8 storage[sizeof(decoded) + 8U];
        memset(storage, 0xa5, sizeof(storage));
        UmicomKernelFat16Timestamp *target = &decoded;
        if (!strcmp(name, "null")) target = NULL;
        else if (!strcmp(name, "alignment")) target = (UmicomKernelFat16Timestamp *)(void *)(storage + 1U);
        else if (!strcmp(name, "extent_overflow")) target =
            (UmicomKernelFat16Timestamp *)(~(UmicomAddress)0U & ~((UmicomAddress)_Alignof(UmicomKernelFat16Timestamp) - 1U));
        else CHECK(0);
        memset(&decoded, 0xa5, sizeof(decoded));
        CHECK(UmicomKernelFat16TimestampDecode(0x747dU, 0x7377U, target) == UMICOM_DISK_INVALID_ARGUMENT);
        MetadataFilled(storage, sizeof(storage), 0xa5U); MetadataFilled(&decoded, sizeof(decoded), 0xa5U);
    }
    CHECK(!reads);
}

static void MetadataGuard(const char *name)
{
    Open(); const UmicomSize beforeReads = reads;
    UmicomKernelFat16 *owner = &volume;
    UmicomKernelFat16Metadata *result = &metadata;
    const char *path = metadataPath;
    static UmicomKernelFat16 copied;
    _Alignas(UmicomKernelFat16Metadata) UmicomU8 unaligned[sizeof(metadata) + 8U];
    memset(unaligned, 0xa5, sizeof(unaligned));
    char unterminated[UMICOM_FAT16_PATH_BYTES]; memset(unterminated, 'A', sizeof(unterminated));
    UmicomKernelDiskStatus expected = UMICOM_DISK_INVALID_ARGUMENT;
    if (!strcmp(name, "null_owner")) owner = NULL;
    else if (!strcmp(name, "owner_alignment")) owner = (UmicomKernelFat16 *)(void *)((UmicomU8 *)&volume + 1U);
    else if (!strcmp(name, "owner_overflow")) owner =
        (UmicomKernelFat16 *)(~(UmicomAddress)0U & ~((UmicomAddress)_Alignof(UmicomKernelFat16) - 1U));
    else if (!strcmp(name, "null_result")) result = NULL;
    else if (!strcmp(name, "result_alignment")) result = (UmicomKernelFat16Metadata *)(void *)(unaligned + 1U);
    else if (!strcmp(name, "result_overflow")) result =
        (UmicomKernelFat16Metadata *)(~(UmicomAddress)0U & ~((UmicomAddress)_Alignof(UmicomKernelFat16Metadata) - 1U));
    else if (!strcmp(name, "result_owner")) result = (UmicomKernelFat16Metadata *)(void *)&volume;
    else if (!strcmp(name, "result_owner_scratch")) result = (UmicomKernelFat16Metadata *)(void *)volume.readStage;
    else if (!strcmp(name, "path_owner")) path = (const char *)&volume;
    else if (!strcmp(name, "path_owner_scratch")) path = (const char *)volume.dataSector;
    else if (!strcmp(name, "path_result")) path = (const char *)&metadata;
    else if (!strcmp(name, "null_path")) path = NULL;
    else if (!strcmp(name, "path_overflow")) path = (const char *)(~(UmicomAddress)0U - 31U);
    else if (!strcmp(name, "unterminated_path")) { path = unterminated; expected = UMICOM_DISK_LIMIT; }
    else if (!strcmp(name, "copied_owner")) { copied = volume; owner = &copied; expected = UMICOM_DISK_BAD_STATE; }
    else if (!strcmp(name, "busy_owner")) { volume.busy = UMICOM_TRUE; expected = UMICOM_DISK_BUSY; }
    else if (!strcmp(name, "closed_owner")) { Close(); expected = UMICOM_DISK_BAD_STATE; }
    else CHECK(0);
    const UmicomKernelFat16 ownerBefore = volume;
    CHECK(UmicomKernelFat16MetadataRead(owner, path, result) == expected);
    CHECK(reads == beforeReads && !memcmp(&metadata, &metadataPrevious, sizeof(metadata)));
    CHECK(!memcmp(&volume, &ownerBefore, sizeof(volume))); MetadataFilled(unaligned, sizeof(unaligned), 0xa5U);
    volume.busy = UMICOM_FALSE; if (volume.open) Close(); MetadataUnchanged();
}
static void MetadataBoundary(const char *name)
{
    struct MetadataBoundaryStorage { char prefix[16]; UmicomKernelFat16Metadata result; } arena, before;
    memset(&arena, 0xa5, sizeof(arena));
    CHECK((const UmicomU8 *)&arena.result == (const UmicomU8 *)&arena + sizeof(arena.prefix));
    if (!strcmp(name, "terminates_before_output")) memcpy(arena.prefix, "/FRAG.BIN", 10U);
    else if (!strcmp(name, "terminator_inside_output")) ((UmicomU8 *)&arena.result)[0] = 0U;
    else CHECK(!strcmp(name, "scans_into_output"));
    memcpy(&before, &arena, sizeof(before)); Open(); const UmicomSize beforeReads = reads;
    const UmicomKernelDiskStatus status = UmicomKernelFat16MetadataRead(&volume, arena.prefix, &arena.result);
    if (!strcmp(name, "terminates_before_output")) {
        CHECK(status == UMICOM_DISK_OK); MetadataExpectedTime(&arena.result.writeTimestamp);
        CHECK(!memcmp(arena.prefix, before.prefix, sizeof(arena.prefix)));
    } else {
        CHECK(status == UMICOM_DISK_INVALID_ARGUMENT && reads == beforeReads);
        CHECK(!memcmp(&arena, &before, sizeof(arena)));
    }
    Close(); MetadataUnchanged();
}
static void MetadataGrammar(const char *name)
{
    const char *path = NULL;
    if (!strcmp(name, "empty")) path = "";
    else if (!strcmp(name, "relative")) path = "FRAG.BIN";
    else if (!strcmp(name, "double_slash")) path = "//FRAG.BIN";
    else if (!strcmp(name, "trailing_slash")) path = "/FRAG.BIN/";
    else if (!strcmp(name, "parent")) path = "/DOCS/../FRAG.BIN";
    else if (!strcmp(name, "dot")) path = "/./FRAG.BIN";
    else if (!strcmp(name, "long_alias")) path = "/NINECHARS.TXT";
    else if (!strcmp(name, "long_extension")) path = "/FRAG.LONG";
    else if (!strcmp(name, "double_extension")) path = "/FRAG.A.B";
    else if (!strcmp(name, "backslash")) path = "/DOCS\\GUIDE.TXT";
    else if (!strcmp(name, "non_ascii")) path = "/\x80";
    else CHECK(0);
    Open(); const UmicomSize before = reads;
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_INVALID_ARGUMENT);
    CHECK(reads == before && !memcmp(&metadata, &metadataPrevious, sizeof(metadata)));
    Close(); MetadataUnchanged();
}
static void MetadataRefusal(const char *name)
{
    UmicomKernelDiskStatus expected = UMICOM_DISK_CORRUPT; const char *path = "/FRAG.BIN";
    if (!strcmp(name, "missing")) { path = "/MISSING.TXT"; expected = UMICOM_DISK_NOT_FOUND; }
    else if (!strcmp(name, "file_as_directory")) { path = "/FRAG.BIN/CHILD"; expected = UMICOM_DISK_NOT_DIRECTORY; }
    else if (!strcmp(name, "unknown_attribute")) Root()[96U + 11U] = 0x80U;
    else if (!strcmp(name, "reserved_case_bits")) Root()[96U + 12U] = 1U;
    else if (!strcmp(name, "high_cluster")) Put16(Root() + 96U + 20U, 1U);
    else if (!strcmp(name, "cluster_outside")) Put16(Root() + 96U + 26U, 0xfffeU);
    else if (!strcmp(name, "nonempty_zero_cluster")) Put16(Root() + 96U + 26U, 0U);
    else if (!strcmp(name, "empty_nonzero_cluster")) Put16(Root() + 128U + 26U, 4U);
    else if (!strcmp(name, "directory_size")) Put32(Root() + 64U + 28U, 1U);
    else if (!strcmp(name, "duplicate_alias")) RootEntry(5U, "FRAG    BIN", 0x20U, 0U, 0U);
    else if (!strcmp(name, "invalid_name_after_target")) {
        RootEntry(5U, "BAD?    BIN", 0x20U, 0U, 0U); expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    }
    else if (!strcmp(name, "parent_cycle")) { Link(3U, 3U); path = "/DOCS/GUIDE.TXT"; expected = UMICOM_DISK_CHAIN_CYCLE; }
    else if (!strcmp(name, "parent_free_link")) { Link(3U, 0U); path = "/DOCS/GUIDE.TXT"; }
    else if (!strcmp(name, "parent_mirror_mismatch")) {
        Fat(1U)[600U] ^= 1U; Link(3U, 300U); path = "/DOCS/GUIDE.TXT"; expected = UMICOM_DISK_FAT_MISMATCH;
    } else if (!strcmp(name, "dot_corrupt")) { Put16(Data(3U) + 26U, 4U); path = "/DOCS/GUIDE.TXT"; }
    else if (!strcmp(name, "directory_capacity")) {
        for (unsigned i = 5U; i < 131U; ++i) {
            char alias[12]; CHECK(snprintf(alias, sizeof(alias), "F%07uTXT", i) == 11);
            RootEntry(i, alias, 0x20U, 0U, 0U);
        }
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "directory_scan_limit")) {
        for (unsigned i = 20U; i < 53U; ++i) Link(i, i == 52U ? 0xffffU : (UmicomU16)(i + 1U));
        Put16(Root() + 64U + 26U, 20U); path = "/DOCS/GUIDE.TXT"; expected = UMICOM_DISK_LIMIT;
    } else CHECK(0);
    MetadataRebase(); Open();
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == expected);
    CHECK(!memcmp(&metadata, &metadataPrevious, sizeof(metadata)) && !volume.busy);
    Close(); MetadataUnchanged();
}
static void MetadataGeometry(const char *name)
{
    Open(); const UmicomKernelFat16 before = volume; const UmicomSize beforeReads = reads;
    if (!strcmp(name, "reader_null")) volume.reader.read = NULL;
    else if (!strcmp(name, "capacity_zero")) volume.reader.sectors = 0U;
    else if (!strcmp(name, "partition_zero")) volume.info.firstSector = 0U;
    else if (!strcmp(name, "partition_outside")) volume.partitionSectors = ~(UmicomU64)0U;
    else if (!strcmp(name, "volume_outside")) volume.info.volumeSectors = ~(UmicomU64)0U;
    else if (!strcmp(name, "cluster_zero")) volume.info.sectorsPerCluster = 0U;
    else if (!strcmp(name, "cluster_nonpower")) volume.info.sectorsPerCluster = 3U;
    else if (!strcmp(name, "cluster_large")) volume.info.sectorsPerCluster = 128U;
    else if (!strcmp(name, "fat_zero")) volume.info.sectorsPerFat = 0U;
    else if (!strcmp(name, "fat_start")) volume.fatStart = ~(UmicomU64)0U;
    else if (!strcmp(name, "root_alignment")) volume.info.rootEntries = 17U;
    else if (!strcmp(name, "root_limit")) volume.info.rootEntries = 528U;
    else if (!strcmp(name, "root_start")) ++volume.rootStart;
    else if (!strcmp(name, "data_start")) ++volume.dataStart;
    else if (!strcmp(name, "cluster_count")) ++volume.info.clusters;
    else CHECK(0);
    CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_BAD_STATE);
    CHECK(reads == beforeReads && !memcmp(&metadata, &metadataPrevious, sizeof(metadata)));
    volume = before; Close(); MetadataUnchanged();
}
static void MetadataLargeFat(void)
{
    static UmicomU8 fat[48U * 512U], root[32U * 512U];
    memcpy(fat, Fat(0U), sizeof(fat)); memcpy(root, Root(), sizeof(root));
    Put16(Boot() + 22U, 257U);
    memset(Boot() + 512U, 0, 514U * 512U);
    memcpy(Boot() + 512U, fat, sizeof(fat)); memcpy(Boot() + 258U * 512U, fat, sizeof(fat));
    memcpy(Boot() + 515U * 512U, root, sizeof(root)); MetadataRebase(); Open();
    CHECK(volume.info.sectorsPerFat == 257U && volume.info.clusters == 11741U);
    CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_OK);
    MetadataExpectedTime(&metadata.writeTimestamp); Close(); MetadataUnchanged();
}
static void MetadataFailures(const char *name)
{
    const char *path = "/FRAG.BIN";
    if (!strcmp(name, "nested_all_reads")) {
        UmicomU8 entry[32]; memcpy(entry, Root() + 96U, sizeof(entry)); Root()[96U] = 0xe5U;
        Link(3U, 300U); Link(300U, 600U); Link(600U, 0xffffU);
        for (unsigned i = 3U; i < 16U; ++i) Data(3U)[i * 32U] = 0xe5U;
        memset(Data(300U), 0x63, 512U); for (unsigned i = 0U; i < 16U; ++i) Data(300U)[i * 32U] = 0xe5U;
        memset(Data(600U), 0, 512U); for (unsigned i = 0U; i < 7U; ++i) Data(600U)[i * 32U] = 0xe5U;
        memcpy(Data(600U) + 7U * 32U, entry, sizeof(entry)); path = "/DOCS/FRAG.BIN";
    } else CHECK(!strcmp(name, "root_all_reads"));
    MetadataRebase(); Open(); reads = 0U;
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_OK);
    const UmicomSize total = reads; CHECK(total >= 2U);
    for (UmicomSize i = 1U; i <= total; ++i) {
        reads = 0U; failRead = i; memset(&metadata, 0xa5, sizeof(metadata));
        CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_IO_ERROR);
        CHECK(reads == i && !volume.busy); MetadataFilled(&metadata, sizeof(metadata), 0xa5U); MetadataUnchanged();
    }
    failRead = 0U; reads = 0U;
    CHECK(UmicomKernelFat16MetadataRead(&volume, path, &metadata) == UMICOM_DISK_OK);
    MetadataExpectedTime(&metadata.writeTimestamp); Close();
    printf("metadata injected-read-failures=%llu\n", (unsigned long long)total);
}
static void MetadataRecheck(const char *name)
{
    Open(); reads = 0U;
    CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_OK);
    metadataCorruptRead = reads; reads = 0U; metadataCorruptOffset = 96U;
    if (!strcmp(name, "attribute")) metadataCorruptOffset += 11U;
    else if (!strcmp(name, "case_bits")) metadataCorruptOffset += 12U;
    else if (!strcmp(name, "high_cluster")) metadataCorruptOffset += 20U;
    else if (!strcmp(name, "cluster")) metadataCorruptOffset += 26U;
    else if (!strcmp(name, "size")) metadataCorruptOffset += 28U;
    else { CHECK(!strcmp(name, "name")); metadataCorruptMask = 1U; /* FRAG -> GRAG remains a valid alias. */ }
    memset(&metadata, 0xa5, sizeof(metadata));
    CHECK(UmicomKernelFat16MetadataRead(&volume, "/FRAG.BIN", &metadata) == UMICOM_DISK_CORRUPT);
    MetadataFilled(&metadata, sizeof(metadata), 0xa5U); CHECK(!volume.busy); Close(); MetadataUnchanged();
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); MetadataStart(argv[2]); const char *const name = argv[1];
    if (!strncmp(name, "decode.", 7U)) MetadataDecode(name + 7U);
    else if (!strncmp(name, "success.", 8U)) MetadataSuccess(name + 8U);
    else if (!strncmp(name, "object.", 7U)) MetadataObject(name + 7U);
    else if (!strncmp(name, "raw.", 4U)) MetadataRaw(name + 4U);
    else if (!strncmp(name, "guard.", 6U)) MetadataGuard(name + 6U);
    else if (!strncmp(name, "boundary.", 9U)) MetadataBoundary(name + 9U);
    else if (!strncmp(name, "grammar.", 8U)) MetadataGrammar(name + 8U);
    else if (!strncmp(name, "refusal.", 8U)) MetadataRefusal(name + 8U);
    else if (!strncmp(name, "geometry.", 9U)) MetadataGeometry(name + 9U);
    else if (!strncmp(name, "failure.", 8U)) MetadataFailures(name + 8U);
    else if (!strncmp(name, "recheck.", 8U)) MetadataRecheck(name + 8U);
    else if (!strcmp(name, "oversized_fat")) MetadataLargeFat();
    else CHECK(0);
    CHECK(!volume.open && !volume.busy); MetadataUnchanged();
    printf("fat16-metadata.%s: ok\n", name); return 0;
}
