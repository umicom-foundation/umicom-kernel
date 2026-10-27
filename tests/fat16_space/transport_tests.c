/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/fat16_space/transport_tests.c
 *
 * PURPOSE:
 *   Verify capacity output only follows complete reads and successful device cleanup.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#define __wrap_main UmicomSpaceOriginalMetadataEntry
#include "../fat16_metadata/transport_tests.c"
#undef __wrap_main
#include "umicom/kernel/disk_space_console.h"

static UmicomBoolean spaceOutputReenter;
static void SpaceOutput(void *context, const char *text, UmicomSize bytes)
{
    if (bytes >= 11U && !memcmp(text, "disk.space.", 11U)) CHECK(Allocated() == 0U);
    MetadataTransportOutput(context, text, bytes);
    if (spaceOutputReenter) {
        const UmicomSize previous = transcriptBytes;
        CHECK(UmicomKernelDiskSpaceInspect(0U, 0U, NULL, SpaceOutput) == UMICOM_DISK_BUSY);
        CHECK(transcriptBytes == previous);
    }
}
static UmicomKernelDiskStatus SpaceInspect(void)
{
    return UmicomKernelDiskSpaceInspect(0U, 0U, NULL, SpaceOutput);
}
static void SpaceNoResult(void)
{
    CHECK(!strstr(transcript, "disk.space."));
    MetadataTransportUnchanged();
}
static void SpaceTransportCase(const char *name)
{
    if (!strcmp(name, "ordinary") || !strcmp(name, "output_reentry") ||
            !strcmp(name, "queue_reentry") || !strcmp(name, "timer_reentry") ||
            !strcmp(name, "policy_reentry") || !strcmp(name, "command_dispatch") ||
            !strcmp(name, "operation_dispatch")) {
        spaceOutputReenter = !strcmp(name, "output_reentry");
        transportOutputReenter = spaceOutputReenter;
        transportQueueReenter = !strcmp(name, "queue_reentry");
        transportTimerReenter = !strcmp(name, "timer_reentry");
        transportPolicyReenter = !strcmp(name, "policy_reentry");
        if (!strcmp(name, "command_dispatch"))
            CHECK(MetadataTransportCommand("fatspace 0 0") == UMICOM_SHELL_OK);
        else if (!strcmp(name, "operation_dispatch"))
            CHECK(UmicomKernelDiskInspect(0U, 0U, "fatspace", "/", NULL, SpaceOutput) == UMICOM_DISK_OK);
        else CHECK(SpaceInspect() == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "total-clusters=12159 free-clusters=12153 allocated-clusters=6 bad-clusters=0"));
        CHECK(strstr(transcript, "data-bytes=6225408 free-bytes=6222336 allocated-bytes=3072 bad-bytes=0"));
        CHECK(strstr(transcript, "disk.inspect=ok") && transportRequests == 100U);
        if (spaceOutputReenter || transportQueueReenter || transportTimerReenter || transportPolicyReenter)
            CHECK(transportReentries);
    } else if (!strcmp(name, "all_read_failures")) {
        for (UmicomSize fault = 1U; fault <= 100U; ++fault) {
            MetadataTransportStart(); transportFailRead = fault;
            CHECK(SpaceInspect() == UMICOM_DISK_IO_ERROR);
            CHECK(transportRequests == fault && !Allocated()); SpaceNoResult();
        }
    } else if (!strcmp(name, "mismatch")) {
        transportMedia[(UMICOM_DISK_FIXTURE_FIRST + 1U + 48U) * 512U + 48U * 512U - 1U] ^= 1U;
        memcpy(transportBefore, transportMedia, sizeof(transportBefore));
        CHECK(SpaceInspect() == UMICOM_DISK_FAT_MISMATCH); SpaceNoResult();
    } else if (!strcmp(name, "reset_retry")) {
        transportResetFault = UMICOM_TRUE;
        CHECK(SpaceInspect() == UMICOM_DISK_IO_ERROR && Allocated() == 2U); SpaceNoResult();
        /* A second invocation cannot publish or discard the retained lease. */
        const UmicomSize requests = transportRequests;
        CHECK(SpaceInspect() == UMICOM_DISK_IO_ERROR && Allocated() == 2U);
        CHECK(transportRequests == requests); SpaceNoResult();
        model.stuckReset = UMICOM_FALSE; transportResetFault = UMICOM_FALSE;
        CHECK(UmicomKernelDiskInspectionClose() == UMICOM_BLOCK_OK && !Allocated());
    } else if (!strcmp(name, "release_retry")) {
        model.failFree = 2U;
        CHECK(SpaceInspect() == UMICOM_DISK_IO_ERROR && Allocated() == 1U); SpaceNoResult();
        model.failFree = 0U;
        CHECK(UmicomKernelDiskInspectionClose() == UMICOM_BLOCK_OK && !Allocated());
    } else if (!strcmp(name, "after_close_deadline") || !strcmp(name, "after_close_rollback")) {
        transportCloseClockFault = UMICOM_TRUE;
        transportClockRollback = !strcmp(name, "after_close_rollback");
        CHECK(SpaceInspect() == UMICOM_DISK_IO_ERROR); SpaceNoResult();
        CHECK(strstr(transcript, transportClockRollback ? "block=clock-error" : "block=timeout"));
    } else if (!strcmp(name, "arguments")) {
        CHECK(UmicomKernelDiskSpaceInspect(UMICOM_BLOCK_SLOT_LIMIT, 0U, NULL, SpaceOutput) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(UmicomKernelDiskSpaceInspect(0U, 4U, NULL, SpaceOutput) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(UmicomKernelDiskSpaceInspect(0U, 0U, NULL, NULL) == UMICOM_DISK_INVALID_ARGUMENT);
        static const char *const bad[] = {"fatspace", "fatspace 0", "fatspace 0 0 /",
            "fatspace -1 0", "fatspace 0 4", "fatspace 99 0", "fatspace 18446744073709551616 0"};
        for (UmicomSize i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
            CHECK(MetadataTransportCommand(bad[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!transportRequests && !transportOutputCalls && !transportTimerCalls);
    } else CHECK(0);
}
int __wrap_main(int argc, char **argv)
{
    CHECK(argc == 3); umicomTransportImage = transportMedia;
    FILE *file = fopen(argv[2], "rb"); CHECK(file);
    CHECK(fread(transportMedia, 1U, sizeof(transportMedia), file) == sizeof(transportMedia));
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    MetadataTransportSeed(); MetadataTransportStart(); SpaceTransportCase(argv[1]);
    CHECK(!Allocated()); MetadataTransportUnchanged();
    printf("fat16-space.transport.%s: pass\n", argv[1]); return 0;
}
