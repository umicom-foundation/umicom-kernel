/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/fat16_file_commit/file_plan_tests.c
 *
 * Native qualification of the production read-only file planner. Real fixture
 * sectors and FAT chains are changed only by this test's fixture construction;
 * the planner receives a read callback with no mutation or release operation.
 * Whole-sector comparisons and injected read failures check publication and
 * preservation independently of the ordered transport's own qualification.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_plan.h"
#include "../disk_inspection/fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)
#define FILE_PLAN_MEDIA_BYTES (UMICOM_DISK_FIXTURE_SECTORS * UMICOM_DISK_SECTOR_BYTES)

static UmicomU8 media[FILE_PLAN_MEDIA_BYTES], mediumBefore[FILE_PLAN_MEDIA_BYTES];
static UmicomU8 input[700U], inputBefore[700U];
static char path[UMICOM_FAT16_PATH_BYTES];
static UmicomKernelFat16FileTime requestedTime;
static UmicomKernelFat16 volume, volumeBefore, volumeCopy;
static UmicomKernelFat16UpdateWorkspace workspace, workspaceBefore;
static UmicomKernelFat16FileUpdateWorkspace fileWorkspace, fileWorkspaceBefore, workspaceCopy;
static UmicomKernelFat16UpdatePlan dataPlan, dataBefore;
static UmicomKernelFat16FileUpdatePlan filePlan, fileBefore;
static UmicomSize requestBytes = 700U;
static UmicomU64 requestOffset = 511U;
static UmicomSize reads, failAt, reentries;
static UmicomBoolean snapshotInputs, reenter;

typedef struct FilePlanArguments {
    UmicomKernelFat16 *volume;
    const char *path;
    const void *input;
    UmicomSize bytes;
    const UmicomKernelFat16FileTime *time;
    UmicomKernelFat16UpdateWorkspace *workspace;
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace;
    UmicomKernelFat16UpdatePlan *data;
    UmicomKernelFat16FileUpdatePlan *file;
} FilePlanArguments;

static UmicomKernelDiskStatus Plan(void);
static void Filled(const void *pointer, UmicomSize bytes, UmicomU8 expected)
{
    const UmicomU8 *const data = pointer;
    for (UmicomSize i = 0U; i < bytes; ++i) CHECK(data[i] == expected);
}
static void Put16(UmicomU8 *target, UmicomU16 value)
{
    target[0] = (UmicomU8)value;
    target[1] = (UmicomU8)(value >> 8U);
}
static UmicomU8 *Sector(UmicomU64 lba)
{
    CHECK(lba < UMICOM_DISK_FIXTURE_SECTORS);
    return media + lba * UMICOM_DISK_SECTOR_BYTES;
}
static UmicomU8 *Cluster(UmicomU16 cluster)
{
    CHECK(cluster >= 2U);
    return Sector(UMICOM_DISK_FIXTURE_DATA + (UmicomU64)cluster - 2U);
}
static void FatLink(UmicomU16 cluster, UmicomU16 next)
{
    for (UmicomSize copy = 0U; copy < 2U; ++copy) {
        UmicomU8 *const fat = Sector(UMICOM_DISK_FIXTURE_FIRST + 1U +
            copy * UMICOM_DISK_FIXTURE_FAT_SECTORS);
        Put16(fat + (UmicomSize)cluster * 2U, next);
    }
}
static void PreserveFields(UmicomU8 *record)
{
    /* Valid, deliberately distinct creation/access fields must survive a
     * write-time update. The high FAT16 cluster word remains zero. */
    record[13] = 197U;
    Put16(record + 14U, 0x1122U);
    Put16(record + 16U, 0x5021U);
    Put16(record + 18U, 0x5123U);
}
static UmicomBoolean Read(void *context, UmicomU64 lba, UmicomU8 *output)
{
    CHECK(context == media);
    ++reads;
    if (reads == failAt) {
        /* An unsuccessful reader may damage scratch. No damaged prefix may
         * reach either output plan, even at the final touched data sector. */
        memset(output, 0xdb, UMICOM_DISK_SECTOR_BYTES);
        return UMICOM_FALSE;
    }
    if (lba >= UMICOM_DISK_FIXTURE_SECTORS) return UMICOM_FALSE;
    memcpy(output, Sector(lba), UMICOM_DISK_SECTOR_BYTES);
    if (reads == 1U && snapshotInputs) {
        memset(input, 0xee, sizeof(input));
        memset(path, 'q', sizeof(path));
        requestedTime = (UmicomKernelFat16FileTime){0};
    }
    if (reads == 1U && reenter) {
        CHECK(Plan() == UMICOM_DISK_BUSY);
        CHECK(UmicomKernelFat16Close(&volume) == UMICOM_DISK_BUSY);
        ++reentries;
    }
    return UMICOM_TRUE;
}
static void Start(void)
{
    memset(&volume, 0, sizeof(volume));
    memset(&workspace, 0, sizeof(workspace));
    memset(&fileWorkspace, 0, sizeof(fileWorkspace));
    memset(&dataPlan, 0xa5, sizeof(dataPlan));
    memset(&filePlan, 0x5a, sizeof(filePlan));
    reads = 0U; failAt = 0U; reentries = 0U;
    snapshotInputs = UMICOM_FALSE; reenter = UMICOM_FALSE;
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, Read, media};
    CHECK(UmicomKernelFat16Open(&volume, &reader, 0U) == UMICOM_DISK_OK);
    reads = 0U;
}
static FilePlanArguments Arguments(void)
{
    return (FilePlanArguments){&volume, path, input, requestBytes, &requestedTime,
        &workspace, &fileWorkspace, &dataPlan, &filePlan};
}
static UmicomKernelDiskStatus Call(const FilePlanArguments *args)
{
    return UmicomKernelFat16PlanFileUpdate(args->volume, args->path, requestOffset,
        args->input, args->bytes, args->time, args->workspace, args->fileWorkspace,
        args->data, args->file);
}
static UmicomKernelDiskStatus Plan(void)
{
    const FilePlanArguments args = Arguments();
    return Call(&args);
}
static void Scrubbed(void)
{
    CHECK(workspace.self == &workspace && fileWorkspace.self == &fileWorkspace);
    Filled((const UmicomU8 *)&workspace + sizeof(workspace.self),
        sizeof(workspace) - sizeof(workspace.self), 0U);
    Filled((const UmicomU8 *)&fileWorkspace + sizeof(fileWorkspace.self),
        sizeof(fileWorkspace) - sizeof(fileWorkspace.self), 0U);
    CHECK(!volume.busy && !volume.fatCached);
}
static void Verify(UmicomU64 directorySector, UmicomSize entryOffset, UmicomBoolean guide)
{
    CHECK(filePlan.directorySector == directorySector && filePlan.entryOffset == entryOffset);
    CHECK(filePlan.requestedTime.year == 2026U && filePlan.requestedTime.month == 10U &&
        filePlan.requestedTime.day == 7U && filePlan.requestedTime.hour == 19U &&
        filePlan.requestedTime.minute == 23U && filePlan.requestedTime.second == 59U);
    CHECK(filePlan.encodedTime.writeTime == 0x9afdU && filePlan.encodedTime.writeDate == 0x5d47U &&
        filePlan.encodedTime.storedSecond == 58U);
    CHECK(!memcmp(filePlan.original, Sector(directorySector), UMICOM_DISK_SECTOR_BYTES));
    UmicomU8 expected[UMICOM_DISK_SECTOR_BYTES];
    memcpy(expected, Sector(directorySector), sizeof(expected));
    expected[entryOffset + 11U] |= 0x20U;
    expected[entryOffset + 22U] = 0xfdU; expected[entryOffset + 23U] = 0x9aU;
    expected[entryOffset + 24U] = 0x47U; expected[entryOffset + 25U] = 0x5dU;
    CHECK(!memcmp(expected, filePlan.data, sizeof(expected)));
    CHECK(dataPlan.count == (guide ? 1U : 3U));
    CHECK(dataPlan.bytes == requestBytes && dataPlan.offset == requestOffset);
    const UmicomU64 sectors[] = {UMICOM_DISK_FIXTURE_DATA + 2U,
        UMICOM_DISK_FIXTURE_DATA + 7U, UMICOM_DISK_FIXTURE_DATA + 4U};
    const UmicomSize offsets[] = {511U, 0U, 0U}, lengths[] = {1U, 512U, 187U};
    UmicomSize used = 0U;
    for (UmicomSize i = 0U; i < dataPlan.count; ++i) {
        const UmicomKernelFat16UpdateSector *const current = &dataPlan.sectors[i];
        const UmicomU64 expectedSector = guide ? UMICOM_DISK_FIXTURE_DATA + 5U : sectors[i];
        const UmicomSize expectedOffset = guide ? requestOffset : offsets[i];
        const UmicomSize expectedBytes = guide ? requestBytes : lengths[i];
        CHECK(current->sector == expectedSector && current->offset == expectedOffset &&
            current->bytes == expectedBytes && current->inputOffset == used);
        memcpy(expected, Sector(expectedSector), sizeof(expected));
        memcpy(expected + expectedOffset, inputBefore + used, expectedBytes);
        CHECK(!memcmp(current->data, expected, sizeof(expected)));
        used += expectedBytes;
    }
    CHECK(used == requestBytes && !memcmp(media, mediumBefore, sizeof(media)));
    Scrubbed();
}
static void Setup(const char *fixture)
{
    FILE *const source = fopen(fixture, "rb"); CHECK(source != NULL);
    CHECK(fread(media, 1U, sizeof(media), source) == sizeof(media));
    CHECK(fgetc(source) == EOF && fclose(source) == 0);
    for (UmicomSize i = 0U; i < sizeof(input); ++i) input[i] = (UmicomU8)((i * 73U + 93U) & 255U);
    memcpy(inputBefore, input, sizeof(input));
    memset(path, 0, sizeof(path)); memcpy(path, "/FRAG.BIN", sizeof("/FRAG.BIN"));
    requestedTime = (UmicomKernelFat16FileTime){2026U,10U,7U,19U,23U,59U};
    PreserveFields(Sector(UMICOM_DISK_FIXTURE_ROOT) + 96U);
}
static void Success(const char *name)
{
    UmicomU64 directory = UMICOM_DISK_FIXTURE_ROOT;
    UmicomSize offset = 96U;
    UmicomBoolean guide = UMICOM_FALSE;
    UmicomU8 *const root = Sector(UMICOM_DISK_FIXTURE_ROOT);
    if (!strcmp(name, "root_archive_clear")) root[96U + 11U] = 0U;
    else if (!strcmp(name, "hidden_system")) root[96U + 11U] = 0x06U;
    else if (!strcmp(name, "nested")) {
        UmicomU8 *const record = Cluster(3U) + 64U;
        record[11U] = 0U; PreserveFields(record);
        memcpy(path, "/DOCS/GUIDE.TXT", sizeof("/DOCS/GUIDE.TXT"));
        directory = UMICOM_DISK_FIXTURE_DATA + 1U; offset = 64U;
        requestOffset = 1U; requestBytes = 37U; guide = UMICOM_TRUE;
    } else if (!strcmp(name, "second_root_sector")) {
        UmicomU8 saved[32U]; memcpy(saved, root + 96U, sizeof(saved));
        root[96U] = 0xe5U;
        for (UmicomSize i = 5U; i < 31U; ++i) root[i * 32U] = 0xe5U;
        memcpy(root + 31U * 32U, saved, sizeof(saved));
        directory += 1U; offset = 480U;
    } else if (!strcmp(name, "fragmented_parent")) {
        UmicomU8 saved[32U]; memcpy(saved, root + 96U, sizeof(saved));
        root[96U] = 0xe5U;
        FatLink(3U, 30U); FatLink(30U, 11U); FatLink(11U, 0xffffU);
        for (UmicomSize i = 3U; i < 16U; ++i) Cluster(3U)[i * 32U] = 0xe5U;
        memset(Cluster(30U), 0xcc, UMICOM_DISK_SECTOR_BYTES);
        memset(Cluster(11U), 0x7b, UMICOM_DISK_SECTOR_BYTES);
        for (UmicomSize i = 0U; i < 16U; ++i) {
            Cluster(30U)[i * 32U] = 0xe5U;
            Cluster(11U)[i * 32U] = 0xe5U;
        }
        memcpy(Cluster(11U) + 480U, saved, sizeof(saved));
        memcpy(path, "/DOCS/FRAG.BIN", sizeof("/DOCS/FRAG.BIN"));
        directory = UMICOM_DISK_FIXTURE_DATA + 9U; offset = 480U;
    } else CHECK(!strcmp(name, "root_archive_set") || !strcmp(name, "input_snapshot") ||
        !strcmp(name, "callback_reentry") || !strcmp(name, "workspace_reuse"));
    memcpy(mediumBefore, media, sizeof(media)); Start();
    snapshotInputs = !strcmp(name, "input_snapshot") ? UMICOM_TRUE : UMICOM_FALSE;
    reenter = !strcmp(name, "callback_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(Plan() == UMICOM_DISK_OK); Verify(directory, offset, guide);
    if (reenter) CHECK(reentries == 1U);
    if (!strcmp(name, "workspace_reuse")) {
        memset(&dataPlan, 0xa5, sizeof(dataPlan)); memset(&filePlan, 0x5a, sizeof(filePlan));
        reads = 0U;
        CHECK(Plan() == UMICOM_DISK_OK); Verify(directory, offset, guide);
    }
}
static void ReadFailures(void)
{
    memcpy(mediumBefore, media, sizeof(media)); Start();
    CHECK(Plan() == UMICOM_DISK_OK);
    const UmicomSize operations = reads;
    CHECK(operations > 3U && operations <= UMICOM_FAT16_IO_LIMIT);
    for (UmicomSize at = 1U; at <= operations; ++at) {
        Start(); failAt = at;
        CHECK(Plan() == UMICOM_DISK_IO_ERROR);
        CHECK(reads == at);
        Filled(&dataPlan, sizeof(dataPlan), 0xa5U); Filled(&filePlan, sizeof(filePlan), 0x5aU);
        CHECK(!memcmp(media, mediumBefore, sizeof(media))); Scrubbed();
    }
    printf("read-failure positions checked: %llu\n", (unsigned long long)operations);
}
static void Refusal(const char *name)
{
    UmicomKernelDiskStatus expected = UMICOM_DISK_READ_ONLY;
    UmicomU8 *const record = Sector(UMICOM_DISK_FIXTURE_ROOT) + 96U;
    if (!strcmp(name, "read_only")) record[11U] = 0x01U;
    else if (!strcmp(name, "old_archive_rule")) record[11U] = 0U;
    else if (!strcmp(name, "orphan")) { FatLink(50U, 0xffffU); expected = UMICOM_DISK_CORRUPT; }
    else if (!strcmp(name, "long_name")) {
        UmicomU8 *const lfn = Sector(UMICOM_DISK_FIXTURE_ROOT) + 160U;
        lfn[0] = 0x41U; lfn[11] = 0x0fU; expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    } else CHECK(0);
    memcpy(mediumBefore, media, sizeof(media)); Start();
    if (!strcmp(name, "old_archive_rule")) {
        CHECK(UmicomKernelFat16PlanUpdate(&volume, path, requestOffset, input, requestBytes,
            &workspace, &dataPlan) == UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
        Filled(&dataPlan, sizeof(dataPlan), 0xa5U);
        CHECK(Plan() == UMICOM_DISK_OK);
        Verify(UMICOM_DISK_FIXTURE_ROOT, 96U, UMICOM_FALSE);
    } else {
        CHECK(Plan() == expected);
        Filled(&dataPlan, sizeof(dataPlan), 0xa5U); Filled(&filePlan, sizeof(filePlan), 0x5aU);
        CHECK(!memcmp(media, mediumBefore, sizeof(media))); Scrubbed();
    }
}
static void SnapshotObjects(void)
{
    memcpy(&volumeBefore, &volume, sizeof(volume));
    memcpy(&workspaceBefore, &workspace, sizeof(workspace));
    memcpy(&fileWorkspaceBefore, &fileWorkspace, sizeof(fileWorkspace));
    memcpy(&dataBefore, &dataPlan, sizeof(dataPlan));
    memcpy(&fileBefore, &filePlan, sizeof(filePlan));
}
static void ObjectsUnchanged(void)
{
    CHECK(!memcmp(&volume, &volumeBefore, sizeof(volume)));
    CHECK(!memcmp(&workspace, &workspaceBefore, sizeof(workspace)));
    CHECK(!memcmp(&fileWorkspace, &fileWorkspaceBefore, sizeof(fileWorkspace)));
    CHECK(!memcmp(&dataPlan, &dataBefore, sizeof(dataPlan)));
    CHECK(!memcmp(&filePlan, &fileBefore, sizeof(filePlan)));
    CHECK(!reads);
}
static void Guard(const char *name)
{
    Start();
    FilePlanArguments args = Arguments();
    UmicomKernelDiskStatus expected = UMICOM_DISK_INVALID_ARGUMENT;
    if (!strcmp(name, "input_file_workspace")) {
        args.input = (UmicomU8 *)&fileWorkspace + sizeof(fileWorkspace) - 1U; args.bytes = 1U;
    } else if (!strcmp(name, "input_file_plan")) {
        args.input = (UmicomU8 *)&filePlan + sizeof(filePlan) - 1U; args.bytes = 1U;
    } else if (!strcmp(name, "time_file_workspace")) args.time = &fileWorkspace.stage.requestedTime;
    else if (!strcmp(name, "time_data_plan")) args.time = (const UmicomKernelFat16FileTime *)&dataPlan;
    else if (!strcmp(name, "result_file_workspace")) args.file = &fileWorkspace.stage;
    else if (!strcmp(name, "workspace_overlap")) args.workspace = (UmicomKernelFat16UpdateWorkspace *)&fileWorkspace;
    else if (!strcmp(name, "outputs_overlap")) args.file = (UmicomKernelFat16FileUpdatePlan *)&dataPlan;
    else if (!strcmp(name, "time_input")) { args.input = &requestedTime; args.bytes = sizeof(requestedTime); }
    else if (!strcmp(name, "path_time")) args.path = (const char *)&requestedTime;
    else if (!strcmp(name, "path_file_plan")) args.path = (const char *)&filePlan;
    else if (!strcmp(name, "unterminated_path")) { memset(path, 'A', sizeof(path)); expected = UMICOM_DISK_LIMIT; }
    else if (!strcmp(name, "invalid_time")) requestedTime = (UmicomKernelFat16FileTime){2100U,2U,29U,0U,0U,0U};
    else if (!strcmp(name, "file_workspace_alignment"))
        args.fileWorkspace = (UmicomKernelFat16FileUpdateWorkspace *)((UmicomU8 *)&fileWorkspace + 1U);
    else if (!strcmp(name, "file_plan_alignment"))
        args.file = (UmicomKernelFat16FileUpdatePlan *)((UmicomU8 *)&filePlan + 1U);
    else if (!strcmp(name, "time_alignment"))
        args.time = (const UmicomKernelFat16FileTime *)((const UmicomU8 *)&requestedTime + 1U);
    else if (!strcmp(name, "input_span_overflow")) args.input = (const void *)(~(UmicomAddress)0U - 7U);
    else if (!strcmp(name, "file_plan_span_overflow")) args.file = (UmicomKernelFat16FileUpdatePlan *)(~(UmicomAddress)0U - 7U);
    else if (!strcmp(name, "time_span_overflow")) args.time = (const UmicomKernelFat16FileTime *)(~(UmicomAddress)0U - 7U);
    else if (!strcmp(name, "dirty_workspace")) { fileWorkspace.path[0] = 'x'; expected = UMICOM_DISK_BAD_STATE; }
    else if (!strcmp(name, "copied_workspace")) {
        memset(&workspaceCopy, 0, sizeof(workspaceCopy));
        workspaceCopy.self = &workspaceCopy; memcpy(&fileWorkspace, &workspaceCopy, sizeof(fileWorkspace));
        expected = UMICOM_DISK_BAD_STATE;
    } else if (!strcmp(name, "copied_volume")) {
        memcpy(&volumeCopy, &volume, sizeof(volumeCopy)); args.volume = &volumeCopy;
        expected = UMICOM_DISK_BAD_STATE;
    } else if (!strcmp(name, "busy_workspace")) { fileWorkspace.busy = UMICOM_TRUE; expected = UMICOM_DISK_BUSY; }
    else if (!strcmp(name, "busy_volume")) { volume.busy = UMICOM_TRUE; expected = UMICOM_DISK_BUSY; }
    else if (!strcmp(name, "null_time")) args.time = NULL;
    else if (!strcmp(name, "null_file_plan")) args.file = NULL;
    else CHECK(0);
    SnapshotObjects();
    CHECK(Call(&args) == expected);
    ObjectsUnchanged();
}
static void PathBoundary(void)
{
    /* The scan begins outside the result and reaches its first byte without
     * encountering a NUL. Refusal must precede any read within result storage. */
    static struct {
        char prefix[8U];
        UmicomKernelFat16FileUpdatePlan result;
    } arena, before;
    Start(); memset(&arena, 'A', sizeof(arena)); memcpy(&before, &arena, sizeof(arena));
    CHECK((const UmicomU8 *)&arena.result == (const UmicomU8 *)&arena + sizeof(arena.prefix));
    FilePlanArguments args = Arguments(); args.path = arena.prefix; args.file = &arena.result;
    SnapshotObjects();
    CHECK(Call(&args) == UMICOM_DISK_INVALID_ARGUMENT);
    CHECK(!memcmp(&arena, &before, sizeof(arena))); ObjectsUnchanged();
}
static void Calendar(const char *name)
{
    UmicomKernelFat16FileTime time = {2026U,10U,7U,19U,23U,59U};
    UmicomKernelFat16FileTimeEncoding encoded;
    if (!strcmp(name, "boundaries")) {
        const UmicomKernelFat16FileTime values[] = {
            {1980U,1U,1U,0U,0U,0U}, {2000U,2U,29U,12U,34U,56U},
            {2100U,2U,28U,23U,59U,59U}, {2107U,12U,31U,23U,59U,59U}};
        const UmicomU16 times[] = {0U,0x645cU,0xbf7dU,0xbf7dU};
        const UmicomU16 dates[] = {0x0021U,0x285dU,0xf05cU,0xff9fU};
        for (UmicomSize i = 0U; i < sizeof(values) / sizeof(values[0]); ++i) {
            CHECK(UmicomKernelFat16FileTimeEncode(&values[i], &encoded) == UMICOM_DISK_OK);
            CHECK(encoded.writeTime == times[i] && encoded.writeDate == dates[i]);
        }
    } else if (!strcmp(name, "second_flooring")) {
        for (UmicomU16 second = 0U; second < 60U; ++second) {
            time.second = second;
            CHECK(UmicomKernelFat16FileTimeEncode(&time, &encoded) == UMICOM_DISK_OK);
            CHECK(encoded.writeDate == 0x5d47U && (encoded.writeTime & 31U) == second / 2U &&
                encoded.storedSecond == (second / 2U) * 2U);
        }
    } else if (!strcmp(name, "invalid_fields")) {
        const UmicomKernelFat16FileTime values[] = {
            {1979U,12U,31U,0U,0U,0U}, {2108U,1U,1U,0U,0U,0U},
            {2026U,0U,1U,0U,0U,0U}, {2026U,13U,1U,0U,0U,0U},
            {2026U,1U,0U,0U,0U,0U}, {2026U,1U,32U,0U,0U,0U},
            {2026U,4U,31U,0U,0U,0U}, {2001U,2U,29U,0U,0U,0U},
            {2100U,2U,29U,0U,0U,0U}, {2026U,10U,7U,24U,0U,0U},
            {2026U,10U,7U,0U,60U,0U}, {2026U,10U,7U,0U,0U,60U},
            {65535U,1U,1U,0U,0U,0U}, {2026U,65535U,1U,0U,0U,0U}};
        for (UmicomSize i = 0U; i < sizeof(values) / sizeof(values[0]); ++i) {
            memset(&encoded, 0xa5, sizeof(encoded));
            CHECK(UmicomKernelFat16FileTimeEncode(&values[i], &encoded) == UMICOM_DISK_INVALID_ARGUMENT);
            Filled(&encoded, sizeof(encoded), 0xa5U);
        }
    } else {
        _Alignas(UmicomKernelFat16FileTime) UmicomU8 arena[64U];
        memset(arena, 0xa5, sizeof(arena));
        memcpy(arena, &time, sizeof(time));
        UmicomU8 before[sizeof(arena)]; memcpy(before, arena, sizeof(before));
        const UmicomKernelFat16FileTime *source = &time;
        UmicomKernelFat16FileTimeEncoding *target = &encoded;
        memset(&encoded, 0xa5, sizeof(encoded));
        if (!strcmp(name, "overlap")) {
            source = (const UmicomKernelFat16FileTime *)arena;
            target = (UmicomKernelFat16FileTimeEncoding *)(arena + 2U);
        } else if (!strcmp(name, "input_alignment")) source = (const UmicomKernelFat16FileTime *)(arena + 1U);
        else if (!strcmp(name, "output_alignment")) target = (UmicomKernelFat16FileTimeEncoding *)(arena + 33U);
        else if (!strcmp(name, "input_span_overflow")) source = (const UmicomKernelFat16FileTime *)(~(UmicomAddress)0U - 7U);
        else if (!strcmp(name, "output_span_overflow")) target = (UmicomKernelFat16FileTimeEncoding *)(~(UmicomAddress)0U - 1U);
        else if (!strcmp(name, "null_input")) source = NULL;
        else if (!strcmp(name, "null_output")) target = NULL;
        else CHECK(0);
        CHECK(UmicomKernelFat16FileTimeEncode(source, target) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(!memcmp(arena, before, sizeof(arena))); Filled(&encoded, sizeof(encoded), 0xa5U);
    }
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    const char *const name = argv[1];
    if (!strncmp(name, "calendar.", 9U)) Calendar(name + 9U);
    else {
        Setup(argv[2]);
        if (!strncmp(name, "success.", 8U)) Success(name + 8U);
        else if (!strncmp(name, "refusal.", 8U)) Refusal(name + 8U);
        else if (!strncmp(name, "guard.", 6U)) Guard(name + 6U);
        else if (!strcmp(name, "all_read_failures")) ReadFailures();
        else if (!strcmp(name, "path_result_boundary")) PathBoundary();
        else CHECK(0);
    }
    printf("FAT16-FILE-PLAN:PASS:%s\n", name);
    return 0;
}
