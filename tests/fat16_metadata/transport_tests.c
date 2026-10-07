/*-----------------------------------------------------------------------------
 * Umicom Kernel — read-only persisted-metadata transport qualification.
 *
 * Reuse the unchanged VirtIO descriptor/register/allocator model. Each queued
 * request is independently required to be READ; complete fixture comparisons
 * include the FATs, directories, file bytes, neighbours and unused space.
 * Console text is published only after successful resource release. Faults
 * exercise real parser, driver, clock, cleanup and command-dispatch paths.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#define UmicomPlatformTimerRead UmicomMetadataOriginalTimerRead
#include "../disk_inspection/transport_tests.c"
#undef UmicomPlatformTimerRead
#include "umicom/kernel/disk_metadata_console.h"

#define METADATA_TRANSPORT_BYTES ((UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U)
static UmicomU8 transportMedia[METADATA_TRANSPORT_BYTES], transportBefore[METADATA_TRANSPORT_BYTES];
static UmicomSize transportRequests, transportFailRead, transportOutputCalls, transportReentries;
static UmicomSize transportTimerCalls, transportTimerFault;
static UmicomBoolean transportQueueReenter, transportPolicyReenter, transportTimerReenter, transportOutputReenter;
static UmicomBoolean transportInsideOutput, transportPathChange, transportResetFault, transportCloseClockFault;
static UmicomBoolean transportClockRollback;
static char transportPath[UMICOM_FAT16_PATH_BYTES];
static void MetadataTransportOutput(void *context, const char *text, UmicomSize bytes);

static UmicomKernelShellStatus MetadataTransportCommand(const char *text)
{
    UmicomKernelConsoleShell shell = {0}; shell.output = MetadataTransportOutput;
    UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus parsed = UmicomKernelShellParse(text, strlen(text), &command);
    if (parsed != UMICOM_SHELL_OK) return parsed;
    const UmicomKernelShellStatus status = UmicomKernelDiskInspectionCommand(&shell, &command, &handled);
    CHECK(handled); return status;
}
static void MetadataTransportReenter(void)
{
    const UmicomSize before = transportOutputCalls, bytes = transcriptBytes;
    CHECK(UmicomKernelDiskMetadataInspect(0U, 0U, "/FRAG.BIN", NULL, MetadataTransportOutput) == UMICOM_DISK_BUSY);
    CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", NULL, MetadataTransportOutput) == UMICOM_DISK_BUSY);
    CHECK(UmicomKernelDiskInspectionClose() == UMICOM_BLOCK_BUSY);
    CHECK(MetadataTransportCommand("diskclose") == UMICOM_SHELL_BUSY);
    CHECK(MetadataTransportCommand("fatstat 0 0 /FRAG.BIN") == UMICOM_SHELL_IO_ERROR);
    CHECK(transportOutputCalls == before && transcriptBytes == bytes); ++transportReentries;
}
UmicomU64 UmicomPlatformTimerRead(void)
{
    ++transportTimerCalls;
    if (transportTimerReenter) MetadataTransportReenter();
    if (transportPathChange) { transportPathChange = UMICOM_FALSE; memcpy(transportPath, "/README.TXT", 12U); }
    if (transportTimerFault && transportTimerCalls == transportTimerFault)
        model.now = transportClockRollback ? 0U : model.now + 100000000U;
    return model.now++;
}
static UmicomBoolean MetadataTransportAllowed(void *context)
{
    CHECK(context == &model);
    if (transportPolicyReenter) MetadataTransportReenter();
    return model.allowed;
}
static void MetadataTransportWrite(void *context, UmicomAddress address, UmicomU32 value)
{
    const UmicomU32 offset = Offset(address);
    if (offset == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        CHECK(*(const UmicomU32 *)header == UMICOM_VIRTIO_REQUEST_READ);
        ++transportRequests; model.result = transportRequests == transportFailRead ? 1U : 0U;
        if (transportQueueReenter) MetadataTransportReenter();
    }
    if (offset == UMICOM_VIRTIO_STATUS && !value && transportRequests) {
        if (transportResetFault) model.stuckReset = UMICOM_TRUE;
        if (transportCloseClockFault) model.now = transportClockRollback ? 0U : model.now + 100000000U;
    }
    WriteRegister(context, address, value);
}
static void MetadataTransportOutput(void *context, const char *text, UmicomSize bytes)
{
    CHECK(!transportInsideOutput); transportInsideOutput = UMICOM_TRUE; ++transportOutputCalls;
    /* A successful metadata line is not observable while DMA is retained. */
    if (bytes >= 10U && !memcmp(text, "disk.file.", 10U)) CHECK(Allocated() == 0U);
    Output(context, text, bytes);
    if (transportOutputReenter) MetadataTransportReenter();
    transportInsideOutput = UMICOM_FALSE;
}
static void MetadataTransportStart(void)
{
    Start(); model.capacity = UMICOM_DISK_FIXTURE_SECTORS;
    transportRequests = 0U; transportFailRead = 0U; transportOutputCalls = 0U; transportReentries = 0U;
    transportTimerCalls = 0U; transportTimerFault = 0U;
    transportQueueReenter = UMICOM_FALSE; transportPolicyReenter = UMICOM_FALSE;
    transportTimerReenter = UMICOM_FALSE; transportOutputReenter = UMICOM_FALSE; transportInsideOutput = UMICOM_FALSE;
    transportPathChange = UMICOM_FALSE; transportResetFault = UMICOM_FALSE; transportCloseClockFault = UMICOM_FALSE;
    transportClockRollback = UMICOM_FALSE;
    memset(transportPath, 0, sizeof(transportPath)); memcpy(transportPath, "/FRAG.BIN", 10U);
    domain.operations.write32 = MetadataTransportWrite; domain.operations.allowed = MetadataTransportAllowed;
}
static void MetadataTransportPut16(UmicomU8 *p, UmicomU16 value)
{
    p[0] = (UmicomU8)value; p[1] = (UmicomU8)(value >> 8U);
}
static UmicomU8 *MetadataTransportRoot(void)
{
    return transportMedia + (UmicomSize)UMICOM_DISK_FIXTURE_ROOT * 512U;
}
static void MetadataTransportSeed(void)
{
    MetadataTransportPut16(MetadataTransportRoot() + 96U + 22U, 0x747dU);
    MetadataTransportPut16(MetadataTransportRoot() + 96U + 24U, 0x7377U);
    memcpy(transportBefore, transportMedia, sizeof(transportBefore));
}
static void MetadataTransportUnchanged(void)
{
    CHECK(!memcmp(transportBefore, transportMedia, sizeof(transportBefore)));
    CHECK(model.observedReadOnlyRequest || model.notifications == 0U);
}
static void MetadataTransportNoMetadata(void)
{
    CHECK(!strstr(transcript, "disk.file.") && !strstr(transcript, "2037-11-23"));
    MetadataTransportUnchanged();
}
static UmicomKernelDiskStatus MetadataTransportInspect(void)
{
    return UmicomKernelDiskMetadataInspect(0U, 0U, transportPath, NULL, MetadataTransportOutput);
}
static void MetadataTransportSuccessful(const char *name)
{
    if (!strcmp(name, "absent")) memset(MetadataTransportRoot() + 96U + 22U, 0, 4U);
    else if (!strcmp(name, "invalid")) MetadataTransportPut16(MetadataTransportRoot() + 96U + 22U, 31U);
    else if (!strcmp(name, "midnight")) MetadataTransportPut16(MetadataTransportRoot() + 96U + 22U, 0U);
    else if (!strcmp(name, "attributes")) MetadataTransportRoot()[96U + 11U] = 0x27U;
    else if (!strcmp(name, "archive_clear")) MetadataTransportRoot()[96U + 11U] = 0U;
    else if (!strcmp(name, "root")) memcpy(transportPath, "/", 2U);
    else if (!strcmp(name, "readonly_file")) memcpy(transportPath, "/README.TXT", 12U);
    else if (!strcmp(name, "path_snapshot")) transportPathChange = UMICOM_TRUE;
    else if (!strcmp(name, "queue_reentry")) transportQueueReenter = UMICOM_TRUE;
    else if (!strcmp(name, "policy_reentry")) transportPolicyReenter = UMICOM_TRUE;
    else if (!strcmp(name, "timer_reentry")) transportTimerReenter = UMICOM_TRUE;
    else if (!strcmp(name, "output_reentry")) transportOutputReenter = UMICOM_TRUE;
    else CHECK(!strcmp(name, "ordinary") || !strcmp(name, "command_dispatch") || !strcmp(name, "operation_dispatch"));
    memcpy(transportBefore, transportMedia, sizeof(transportBefore));
    if (!strcmp(name, "command_dispatch")) CHECK(MetadataTransportCommand("fatstat 0 0 /FRAG.BIN") == UMICOM_SHELL_OK);
    else if (!strcmp(name, "operation_dispatch")) CHECK(UmicomKernelDiskInspect(0U, 0U, "fatstat", transportPath,
        NULL, MetadataTransportOutput) == UMICOM_DISK_OK);
    else CHECK(MetadataTransportInspect() == UMICOM_DISK_OK);
    CHECK(!Allocated() && strstr(transcript, "disk.inspect=ok block=ok close=ok"));
    if (!strcmp(name, "root")) {
        CHECK(strstr(transcript, "name=/ kind=directory bytes=0 first-cluster=0 attributes=0x00"));
        CHECK(strstr(transcript, "directory-entry=synthetic-root") && strstr(transcript, "write-time=absent"));
    } else if (!strcmp(name, "readonly_file")) {
        CHECK(strstr(transcript, "name=README.TXT kind=file") && strstr(transcript, "read-only=1"));
        CHECK(strstr(transcript, "write-state=absent raw-date=0x0000 raw-time=0x0000"));
    } else {
        CHECK(strstr(transcript, "name=FRAG.BIN kind=file bytes=1300 first-cluster=4"));
        CHECK(strstr(transcript, "directory-entry=present"));
        if (!strcmp(name, "absent")) CHECK(strstr(transcript, "write-state=absent raw-date=0x0000 raw-time=0x0000") && strstr(transcript, "write-time=absent"));
        else if (!strcmp(name, "invalid")) CHECK(strstr(transcript, "write-state=invalid raw-date=0x7377 raw-time=0x001f") && strstr(transcript, "write-time=invalid"));
        else if (!strcmp(name, "midnight")) CHECK(strstr(transcript, "write-time=2037-11-23T00:00:00"));
        else CHECK(strstr(transcript, "write-state=valid raw-date=0x7377 raw-time=0x747d") && strstr(transcript, "write-time=2037-11-23T14:35:58"));
        if (!strcmp(name, "attributes")) CHECK(strstr(transcript, "attributes=0x27") && strstr(transcript, "read-only=1 hidden=1 system=1 directory=0 archive=1"));
        else if (!strcmp(name, "archive_clear")) CHECK(strstr(transcript, "attributes=0x00") && strstr(transcript, "archive=0"));
        else CHECK(strstr(transcript, "attributes=0x20") && strstr(transcript, "read-only=0 hidden=0 system=0 directory=0 archive=1"));
    }
    if (transportQueueReenter || transportPolicyReenter || transportTimerReenter || transportOutputReenter) CHECK(transportReentries > 0U);
    if (!strcmp(name, "path_snapshot")) CHECK(!strcmp(transportPath, "/README.TXT"));
    MetadataTransportUnchanged();
}
static void MetadataTransportFailure(const char *name)
{
    if (!strcmp(name, "writable_refused")) model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    else if (!strcmp(name, "bad_completion")) model.usedId = 1U;
    else if (!strcmp(name, "unsafe_context")) model.allowed = UMICOM_FALSE;
    else if (!strcmp(name, "allocation_first")) model.failAllocate = 1U;
    else if (!strcmp(name, "allocation_second")) model.failAllocate = 2U;
    else if (!strcmp(name, "dirty")) {
        for (unsigned copy = 0U; copy < 2U; ++copy)
            MetadataTransportPut16(transportMedia + ((UmicomSize)UMICOM_DISK_FIXTURE_FIRST + 1U +
                copy * UMICOM_DISK_FIXTURE_FAT_SECTORS) * 512U + 2U, 0x7fffU);
    } else if (!strcmp(name, "bad_signature")) transportMedia[510U] = 0U;
    else if (!strcmp(name, "missing")) memcpy(transportPath, "/MISSING.TXT", 13U);
    else if (!strcmp(name, "malicious_name")) MetadataTransportRoot()[96U] = 27U;
    else CHECK(0);
    memcpy(transportBefore, transportMedia, sizeof(transportBefore));
    CHECK(MetadataTransportInspect() != UMICOM_DISK_OK); MetadataTransportNoMetadata();
    CHECK(!Allocated() && !strchr(transcript, 27));
    if (!strcmp(name, "dirty")) CHECK(strstr(transcript, "disk.inspect=unclean-volume"));
    if (!strcmp(name, "writable_refused")) CHECK(!transportRequests && strstr(transcript, "writable-device-refused"));
    if (!strcmp(name, "missing")) CHECK(strstr(transcript, "disk.inspect=not-found"));
}
static void MetadataTransportReads(void)
{
    CHECK(MetadataTransportInspect() == UMICOM_DISK_OK); const UmicomSize total = transportRequests;
    CHECK(total >= 6U && !Allocated());
    for (UmicomSize i = 1U; i <= total; ++i) {
        MetadataTransportStart(); transportFailRead = i;
        CHECK(MetadataTransportInspect() == UMICOM_DISK_IO_ERROR);
        CHECK(transportRequests == i && !Allocated()); MetadataTransportNoMetadata();
    }
    printf("metadata transport injected-read-failures=%llu\n", (unsigned long long)total);
}
static void MetadataTransportCleanup(const char *name)
{
    if (!strcmp(name, "reset_retry") || !strcmp(name, "retained_blocks_next")) transportResetFault = UMICOM_TRUE;
    else if (!strcmp(name, "first_release_retry")) model.failFree = 1U;
    else if (!strcmp(name, "second_release_retry")) model.failFree = 2U;
    else CHECK(0);
    CHECK(MetadataTransportInspect() == UMICOM_DISK_IO_ERROR); MetadataTransportNoMetadata();
    CHECK(Allocated() == (!strcmp(name, "second_release_retry") ? 1U : 2U));
    if (!strcmp(name, "retained_blocks_next")) {
        const UmicomSize before = transportRequests;
        CHECK(MetadataTransportInspect() == UMICOM_DISK_IO_ERROR);
        CHECK(transportRequests == before && Allocated() == 2U); MetadataTransportNoMetadata();
    }
    transportResetFault = UMICOM_FALSE; model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    CHECK(MetadataTransportCommand("diskclose") == UMICOM_SHELL_OK && !Allocated());
    memset(transcript, 0, sizeof(transcript)); transcriptBytes = 0U;
    CHECK(MetadataTransportInspect() == UMICOM_DISK_OK && !Allocated());
    CHECK(strstr(transcript, "write-time=2037-11-23T14:35:58")); MetadataTransportUnchanged();
}
static void MetadataTransportClock(const char *name)
{
    CHECK(MetadataTransportInspect() == UMICOM_DISK_OK); const UmicomSize calls = transportTimerCalls;
    CHECK(calls >= 3U && !Allocated()); MetadataTransportStart();
    transportClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    if (!strncmp(name, "last_read_", 10U)) transportTimerFault = calls - 2U;
    else if (!strncmp(name, "acceptance_", 11U)) transportTimerFault = calls - 1U;
    else if (!strncmp(name, "after_close_", 12U)) transportCloseClockFault = UMICOM_TRUE;
    else CHECK(0);
    CHECK(MetadataTransportInspect() == UMICOM_DISK_IO_ERROR);
    CHECK(!Allocated()); MetadataTransportNoMetadata();
    CHECK(strstr(transcript, transportClockRollback ? "block=clock-error" : "block=timeout"));
}
static void MetadataTransportArguments(const char *name)
{
    if (!strcmp(name, "commands")) {
        static const char *const bad[] = {"fatstat", "fatstat 0", "fatstat 0 0",
            "fatstat -1 0 /FRAG.BIN", "fatstat 0 -1 /FRAG.BIN", "fatstat 99 0 /FRAG.BIN", "fatstat 0 4 /FRAG.BIN",
            "fatstat 18446744073709551616 0 /FRAG.BIN"};
        for (UmicomSize i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
            CHECK(MetadataTransportCommand(bad[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(MetadataTransportCommand("fatstat 0 0 /FRAG.BIN extra") == UMICOM_SHELL_SYNTAX);
    } else {
        const char *path = transportPath; UmicomSize slot = 0U, partition = 0U;
        UmicomKernelBlockOutput output = MetadataTransportOutput;
        char unterminated[UMICOM_FAT16_PATH_BYTES]; memset(unterminated, 'A', sizeof(unterminated));
        UmicomKernelDiskStatus expected = UMICOM_DISK_INVALID_ARGUMENT;
        if (!strcmp(name, "null_path")) path = NULL;
        else if (!strcmp(name, "path_overflow")) path = (const char *)(~(UmicomAddress)0U - 31U);
        else if (!strcmp(name, "null_output")) output = NULL;
        else if (!strcmp(name, "slot")) slot = UMICOM_BLOCK_SLOT_LIMIT;
        else if (!strcmp(name, "partition")) partition = UMICOM_DISK_PRIMARY_PARTITIONS;
        else if (!strcmp(name, "empty_path")) path = "";
        else if (!strcmp(name, "relative_path")) path = "FRAG.BIN";
        else if (!strcmp(name, "unterminated_path")) { path = unterminated; expected = UMICOM_DISK_LIMIT; }
        else CHECK(0);
        CHECK(UmicomKernelDiskMetadataInspect(slot, partition, path, NULL, output) == expected);
    }
    CHECK(!transportRequests && !transportOutputCalls && !transportTimerCalls && !model.reads && !model.writes && !Allocated());
}
/* The ELF startup reference selects this suite without altering the included
 * regression entry or the original transport/model sources. */
int __wrap_main(int argc, char **argv)
{
    CHECK(argc == 3); umicomTransportImage = transportMedia;
    FILE *file = fopen(argv[2], "rb"); CHECK(file);
    CHECK(fread(transportMedia, 1U, sizeof(transportMedia), file) == sizeof(transportMedia));
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    MetadataTransportSeed(); MetadataTransportStart(); const char *const name = argv[1];
    if (!strncmp(name, "success.", 8U)) MetadataTransportSuccessful(name + 8U);
    else if (!strncmp(name, "failure.", 8U)) MetadataTransportFailure(name + 8U);
    else if (!strncmp(name, "cleanup.", 8U)) MetadataTransportCleanup(name + 8U);
    else if (!strncmp(name, "clock.", 6U)) MetadataTransportClock(name + 6U);
    else if (!strncmp(name, "arguments.", 10U)) MetadataTransportArguments(name + 10U);
    else if (!strcmp(name, "all_read_failures")) MetadataTransportReads();
    else CHECK(0);
    CHECK(!Allocated()); MetadataTransportUnchanged();
    printf("fat16-metadata.transport.%s: ok\n", name); return 0;
}
