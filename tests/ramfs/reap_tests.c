/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/ramfs/reap_tests.c
 *
 * PURPOSE:
 *   Keep cleanup counters outside filesystem ownership and preserve retryable reclamation.
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
static void ReapCase(const char *name)
{
    const UmicomKernelFileDescriptor fd = UmicomFile("/sample");
    UmicomWriteFile(fd, 0U, 4200U);
    UmicomKernelRamfsNode *const node = UmicomFirstNode();
    UmicomSize count = 71U;
    if (!strcmp(name, "linked") || !strcmp(name, "pinned") ||
        !strcmp(name, "unlinked") || !strcmp(name, "closing") ||
        !strcmp(name, "release_retry")) {
        if (strcmp(name, "linked"))
            OK(UmicomKernelVfsRemove(&umicomClient, "/sample", UMICOM_VFS_FILE));
        const UmicomBoolean retained = !strcmp(name, "linked") || !strcmp(name, "pinned");
        if (!retained) OK(UmicomKernelVfsClose(&umicomClient, fd));
        if (!strcmp(name, "closing")) umicomFs.state = UMICOM_VFS_CLOSING;
        if (!strcmp(name, "release_retry")) {
            /* Refuse the second page after the first was released. Ownership
             * must survive so a second call can finish exactly one node. */
            umicomFreeCall = 0U; umicomFailFree = 2U;
            CHECK(UmicomKernelRamfsReap(&umicomFs, &count) == UMICOM_VFS_RELEASE_FAILED);
            CHECK(count == 0U && umicomFs.nodes == 2U && umicomFs.dataPages == 1U);
            umicomFailFree = 0U;
        }
        OK(UmicomKernelRamfsReap(&umicomFs, &count));
        CHECK(count == (retained ? 0U : 1U));
        CHECK(umicomFs.nodes == (retained ? 2U : 1U));
        CHECK(umicomFs.dataPages == (retained ? 2U : 0U));
        umicomFs.state = UMICOM_VFS_OPEN;
        return;
    }
    UmicomKernelRamfs *owner = &umicomFs;
    UmicomSize *result = &count;
    UmicomKernelRamfs copy = umicomFs;
    UmicomKernelVfsStatus expected = UMICOM_VFS_INVALID_ARGUMENT;
    if (!strcmp(name, "null_owner")) owner = NULL;
    else if (!strcmp(name, "null_result")) result = NULL;
    else if (!strcmp(name, "owner_alignment")) owner = (UmicomKernelRamfs *)((UmicomU8 *)&umicomFs + 1U);
    else if (!strcmp(name, "result_alignment")) result = (UmicomSize *)((UmicomU8 *)&count + 1U);
    else if (!strcmp(name, "owner_overflow")) owner = (UmicomKernelRamfs *)(~(UmicomAddress)0U - 7U);
    else if (!strcmp(name, "result_overflow")) result = (UmicomSize *)(~(UmicomAddress)0U - 7U);
    else if (!strcmp(name, "owner_alias")) result = (UmicomSize *)&umicomFs;
    else if (!strcmp(name, "root_alias")) result = &umicomFs.root.pins;
    else if (!strcmp(name, "cache_alias")) result = &umicomFs.nodeCache.frames;
    else if (!strcmp(name, "node_alias")) result = &node->bytes;
    else if (!strcmp(name, "cache_page_alias")) result = (UmicomSize *)umicomFs.nodeCache.pages[0].frame;
    else if (!strcmp(name, "data_alias")) result = (UmicomSize *)node->pages[0];
    else if (!strcmp(name, "data_tail_alias")) result = (UmicomSize *)(node->pages[0] + 4088U);
    else if (!strcmp(name, "second_page_alias")) result = (UmicomSize *)node->pages[1];
    else if (!strcmp(name, "copied")) { owner = &copy; expected = UMICOM_VFS_BAD_STATE; }
    else if (!strcmp(name, "unsafe")) { umicomAllowed = UMICOM_FALSE; expected = UMICOM_VFS_UNSAFE_CONTEXT; }
    else CHECK(0);
    memcpy(arenaBefore, umicomArena, sizeof(umicomArena));
    memcpy(&ownerBefore, &umicomFs, sizeof(umicomFs));
    const unsigned freesBefore = umicomFreeCall;
    CHECK(UmicomKernelRamfsReap(owner, result) == expected);
    CHECK(count == 71U && umicomFreeCall == freesBefore);
    CHECK(!memcmp(&ownerBefore, &umicomFs, sizeof(umicomFs)));
    CHECK(!memcmp(arenaBefore, umicomArena, sizeof(umicomArena)));
    umicomAllowed = UMICOM_TRUE;
    OK(UmicomKernelRamfsValidate(&umicomFs));
    CHECK(UmicomReadFile(fd, 0U, 4200U) == 4200U);
    CHECK(!memcmp(umicomRead, umicomData, 4200U));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); UmicomSetup(); ReapCase(argv[1]); UmicomFinish();
    printf("ramfs.reap.%s: pass\n", argv[1]); return 0;
}
