/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/ramfs/snapshot_tests.c
 *
 * PURPOSE:
 *   Reject snapshot storage aliases without damaging live file or metadata ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#define main UmicomRamfsOriginalRegressionEntry
#include "ramfs_tests.c"
#undef main

static UmicomU8 arenaBefore[sizeof(umicomArena)];
static UmicomKernelRamfs ownerBefore;
static UmicomKernelRamfsInfo snapshot, snapshotBefore;

static void SnapshotCase(const char *name)
{
    const UmicomKernelFileDescriptor fd = UmicomFile("/sample");
    UmicomWriteFile(fd, 0U, 4200U);
    UmicomKernelRamfsNode *const node = UmicomFirstNode();
    if (!strcmp(name, "ordinary") || !strcmp(name, "unlinked") || !strcmp(name, "closing")) {
        /* Removal states the expected node kind, preserving typed namespace
         * authority. Keep the incomplete invocation disabled for comparison. */
#if 0
        if (!strcmp(name, "unlinked")) OK(UmicomKernelVfsRemove(&umicomClient, "/sample"));
#endif
        if (!strcmp(name, "unlinked")) OK(UmicomKernelVfsRemove(&umicomClient, "/sample", UMICOM_VFS_FILE));
        if (!strcmp(name, "closing")) umicomFs.state = UMICOM_VFS_CLOSING;
        OK(UmicomKernelRamfsSnapshot(&umicomFs, &snapshot));
        CHECK(snapshot.nodes == 2U && snapshot.dataPages == 2U && snapshot.metadataPages == 1U);
        CHECK(snapshot.pins == 2U && snapshot.namespaceEpoch == umicomFs.epoch);
        CHECK(snapshot.unlinkedNodes == (!strcmp(name, "unlinked") ? 1U : 0U));
        umicomFs.state = UMICOM_VFS_OPEN;
        return;
    }
    UmicomKernelRamfs *owner = &umicomFs;
    UmicomKernelRamfsInfo *result = &snapshot;
    UmicomKernelVfsStatus expected = UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfs copied = umicomFs;
    if (!strcmp(name, "null_owner")) owner = NULL;
    else if (!strcmp(name, "null_result")) result = NULL;
    else if (!strcmp(name, "owner_alignment")) owner = (UmicomKernelRamfs *)((UmicomU8 *)&umicomFs + 1U);
    else if (!strcmp(name, "result_alignment")) result = (UmicomKernelRamfsInfo *)((UmicomU8 *)&snapshot + 1U);
    else if (!strcmp(name, "owner_overflow")) owner = (UmicomKernelRamfs *)(~(UmicomAddress)0U - 15U);
    else if (!strcmp(name, "result_overflow")) result = (UmicomKernelRamfsInfo *)(~(UmicomAddress)0U - 15U);
    else if (!strcmp(name, "owner_alias")) result = (UmicomKernelRamfsInfo *)&umicomFs;
    else if (!strcmp(name, "root_alias")) result = (UmicomKernelRamfsInfo *)&umicomFs.root;
    else if (!strcmp(name, "cache_alias")) result = (UmicomKernelRamfsInfo *)&umicomFs.nodeCache;
    else if (!strcmp(name, "node_alias")) result = (UmicomKernelRamfsInfo *)node;
    else if (!strcmp(name, "cache_page_alias")) result = (UmicomKernelRamfsInfo *)umicomFs.nodeCache.pages[0].frame;
    else if (!strcmp(name, "data_alias")) result = (UmicomKernelRamfsInfo *)node->pages[0];
    else if (!strcmp(name, "data_tail_alias")) result = (UmicomKernelRamfsInfo *)(node->pages[0] + 4088U);
    else if (!strcmp(name, "second_page_alias")) result = (UmicomKernelRamfsInfo *)node->pages[1];
    else if (!strcmp(name, "copied")) { owner = &copied; expected = UMICOM_VFS_BAD_STATE; }
    else if (!strcmp(name, "unsafe")) { umicomAllowed = UMICOM_FALSE; expected = UMICOM_VFS_UNSAFE_CONTEXT; }
    else if (!strcmp(name, "pin_overflow")) {
        umicomFs.root.pins = ~(UmicomSize)0U; expected = UMICOM_VFS_RANGE;
    } else CHECK(0);
    memset(&snapshot, 0xa5, sizeof(snapshot)); memcpy(&snapshotBefore, &snapshot, sizeof(snapshot));
    memcpy(arenaBefore, umicomArena, sizeof(umicomArena)); memcpy(&ownerBefore, &umicomFs, sizeof(umicomFs));
    CHECK(UmicomKernelRamfsSnapshot(owner, result) == expected);
    CHECK(!memcmp(&snapshot, &snapshotBefore, sizeof(snapshot)));
    CHECK(!memcmp(&umicomFs, &ownerBefore, sizeof(umicomFs)));
    CHECK(!memcmp(umicomArena, arenaBefore, sizeof(umicomArena)));
    umicomAllowed = UMICOM_TRUE;
    if (!strcmp(name, "pin_overflow")) umicomFs.root.pins = 1U;
    OK(UmicomKernelRamfsValidate(&umicomFs));
    CHECK(UmicomReadFile(fd, 0U, 4200U) == 4200U);
    CHECK(!memcmp(umicomRead, umicomData, 4200U));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); UmicomSetup(); SnapshotCase(argv[1]); UmicomFinish();
    printf("ramfs.snapshot.%s: pass\n", argv[1]); return 0;
}
