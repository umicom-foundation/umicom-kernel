/*-----------------------------------------------------------------------------
 * Umicom Kernel VFS/RAMFS native regression tests
 * File: tests/ramfs/ramfs_tests.c
 *
 * The namespace, descriptors, cache, frame bitmap and file data are real C
 * implementations. Only machine-state admission and selected allocator failures
 * are modelled. Metadata corruption below is deliberate test injection, not a
 * supported way for a service to alter a live filesystem.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/ramfs.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == UMICOM_VFS_OK)
#define RW (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE)
#define ARENA_PAGES 512U
alignas(4096) static UmicomU8 umicomArena[ARENA_PAGES * 4096U];
static UmicomKernelRamfs umicomFs;
static UmicomKernelVfs umicomVfs;
static UmicomKernelVfsClient umicomClient;
static UmicomKernelVfsClient umicomOther;
static UmicomKernelVfsClient umicomClients[UMICOM_VFS_CLIENT_LIMIT];
static UmicomBoolean umicomAllowed = UMICOM_TRUE;
static long umicomAllocateBudget = -1;
static unsigned umicomFreeCall;
static unsigned umicomFailFree;
static UmicomU8 umicomData[UMICOM_RAMFS_FILE_BYTES];
static UmicomU8 umicomRead[UMICOM_RAMFS_FILE_BYTES];
static UmicomU8 umicomModel[UMICOM_RAMFS_FILE_BYTES];

UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void) { return umicomAllowed; }
UmicomKernelMemoryStatus UmicomVfsActualAllocate(UmicomAddress *outFrame);
UmicomKernelMemoryStatus UmicomVfsActualFree(UmicomAddress frame);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *outFrame)
{
    if (umicomAllocateBudget == 0) return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
    UmicomKernelMemoryStatus result = UmicomVfsActualAllocate(outFrame);
    if (result == UMICOM_KERNEL_MEMORY_OK && umicomAllocateBudget > 0) --umicomAllocateBudget;
    return result;
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame)
{
    ++umicomFreeCall;
    if (umicomFailFree != 0U && umicomFreeCall == umicomFailFree) return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    return UmicomVfsActualFree(frame);
}
static UmicomSize UmicomAllocated(void)
{
    UmicomKernelPhysicalMemorySnapshot state = {0};
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&state) == UMICOM_KERNEL_MEMORY_OK);
    return state.allocatedFrames;
}
static void UmicomSetup(void)
{
    /* Reset only an isolated test arena, never a live production namespace. */
    memset(&umicomFs, 0, sizeof(umicomFs));
    memset(&umicomVfs, 0, sizeof(umicomVfs));
    memset(&umicomClient, 0, sizeof(umicomClient));
    memset(&umicomOther, 0, sizeof(umicomOther));
    memset(umicomClients, 0, sizeof(umicomClients));
    memset(umicomArena, 0xcc, sizeof(umicomArena));
    for (UmicomSize i = 0U; i < sizeof(umicomData); ++i) umicomData[i] = (UmicomU8)(i * 17U + 3U);
    memset(umicomRead, 0xee, sizeof(umicomRead));
    memset(umicomModel, 0, sizeof(umicomModel));
    umicomAllowed = UMICOM_TRUE;
    umicomAllocateBudget = -1;
    umicomFailFree = 0U;
    umicomFreeCall = 0U;
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomArena, sizeof(umicomArena)) == UMICOM_KERNEL_MEMORY_OK);
    OK(UmicomKernelRamfsInitialize(&umicomFs));
    OK(UmicomKernelVfsMount(&umicomVfs, UmicomKernelRamfsOperations(), &umicomFs));
    OK(UmicomKernelVfsClientOpen(&umicomClient, &umicomVfs, 101U, UMICOM_VFS_RIGHT_ALL));
}
static UmicomKernelFileDescriptor UmicomFile(const char *path)
{
    UmicomKernelFileDescriptor result = 0U;
    OK(UmicomKernelVfsCreate(&umicomClient, path, UMICOM_VFS_FILE));
    OK(UmicomKernelVfsOpen(&umicomClient, path, RW, UMICOM_FALSE, &result));
    CHECK(result != 0U);
    return result;
}
static UmicomSize UmicomReadFile(UmicomKernelFileDescriptor fd, UmicomSize offset, UmicomSize bytes)
{
    UmicomSize result = 999U;
    OK(UmicomKernelVfsSeek(&umicomClient, fd, offset));
    OK(UmicomKernelVfsRead(&umicomClient, fd, umicomRead, bytes, &result));
    return result;
}
static void UmicomWriteFile(UmicomKernelFileDescriptor fd, UmicomSize offset, UmicomSize bytes)
{
    UmicomSize result = 999U;
    OK(UmicomKernelVfsSeek(&umicomClient, fd, offset));
    OK(UmicomKernelVfsWrite(&umicomClient, fd, umicomData, bytes, &result));
    CHECK(result == bytes);
}
static void UmicomFinish(void)
{
    umicomAllocateBudget = -1;
    umicomFailFree = 0U;
    UmicomSize closed = 0U;
    if (umicomOther.state == UMICOM_VFS_OPEN) OK(UmicomKernelVfsClientClose(&umicomOther, &closed));
    for (UmicomSize i = 0U; i < UMICOM_VFS_CLIENT_LIMIT; ++i) {
        if (umicomClients[i].state == UMICOM_VFS_OPEN) OK(UmicomKernelVfsClientClose(&umicomClients[i], &closed));
    }
    if (umicomClient.state == UMICOM_VFS_OPEN) OK(UmicomKernelVfsClientClose(&umicomClient, &closed));
    if (umicomVfs.state == UMICOM_VFS_OPEN) OK(UmicomKernelVfsUnmount(&umicomVfs));
    if (umicomFs.state != UMICOM_VFS_CLOSED) OK(UmicomKernelRamfsClose(&umicomFs));
    CHECK(UmicomAllocated() == 0U);
}
static UmicomKernelRamfsNode *UmicomFirstNode(void)
{
    CHECK(umicomFs.records[1].id != 0U);
    return (UmicomKernelRamfsNode *)umicomFs.records[1].object.address;
}
static const char *const umicomCases[] = {
    "lazy-initialisation", "reinitialisation-refused", "copied-owner", "empty-directory",
    "nested-paths", "invalid-paths", "component-boundary", "depth-boundary", "duplicate-create",
    "file-as-directory", "nonempty-directory", "open-directory-removal", "descriptor-types",
    "client-rights", "descriptor-rights", "duplicate-rights", "shared-offset", "independent-offset",
    "client-local-tokens", "stale-descriptor", "generation-exhaustion", "descriptor-capacity",
    "client-capacity", "eof", "partial-eof", "binary-bytes", "cross-page-io",
    "noncontiguous-pages", "sparse-gap", "sparse-growth", "shrink-regrow-zeroes", "append",
    "maximum-file", "partial-allocation-failure", "data-page-quota", "node-quota",
    "unlink-open-recreate", "reap-pinned-file", "reap-release-retry", "close-release-positions",
    "close-busy", "client-close-pins", "iteration-change", "entry-snapshot", "root-removal",
    "null-arguments", "range-overflow", "zero-io", "unsafe-context", "missing-backing-frame",
    "aliased-backing-frame", "invalid-parent", "damaged-cache-guard", "descriptor-count-corruption",
    "epoch-exhaustion", "identity-exhaustion", "multiple-mount-pins", "repeated-lifetimes",
    "mixed-byte-model", "allocation-budgets", "missing-provider-operation", "outputs-on-refusal",
    "metadata-data-alias", "cache-release-retry", "directory-duplicate-cursor"
};
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    UmicomSize selected = sizeof(umicomCases) / sizeof(umicomCases[0]);
    for (UmicomSize i = 0U; i < sizeof(umicomCases) / sizeof(umicomCases[0]); ++i) if (!strcmp(argv[1], umicomCases[i])) selected = i;
    if (selected == sizeof(umicomCases) / sizeof(umicomCases[0])) return 2;
    UmicomSetup();
    UmicomSize count = 0U;
    UmicomKernelFileDescriptor fd = 0U;
    UmicomKernelFileDescriptor second = 0U;
    UmicomKernelVfsNodeInfo info = {0};
    UmicomKernelVfsDirectoryEntry entry = {0};
    switch (selected) {
        case 0: {
            UmicomKernelRamfsInfo snapshot = {0};
            OK(UmicomKernelRamfsSnapshot(&umicomFs, &snapshot));
            CHECK(snapshot.nodes == 1U && snapshot.dataPages == 0U && snapshot.metadataPages == 0U && snapshot.pins == 1U);
            CHECK(UmicomAllocated() == 0U); break;
        }
        case 1:
            CHECK(UmicomKernelRamfsInitialize(&umicomFs) == UMICOM_VFS_BAD_STATE);
            CHECK(UmicomKernelVfsMount(&umicomVfs, UmicomKernelRamfsOperations(), &umicomFs) == UMICOM_VFS_BAD_STATE);
            CHECK(UmicomKernelVfsClientOpen(&umicomClient, &umicomVfs, 5U, RW) == UMICOM_VFS_BAD_STATE); break;
        case 2: {
            static UmicomKernelRamfs copy;
            copy = umicomFs;
            CHECK(UmicomKernelRamfsValidate(&copy) == UMICOM_VFS_BAD_STATE);
            umicomOther = umicomClient;
            CHECK(UmicomKernelVfsClientValidate(&umicomOther) == UMICOM_VFS_BAD_STATE);
            memset(&umicomOther, 0, sizeof(umicomOther)); break;
        }
        case 3:
            OK(UmicomKernelVfsOpen(&umicomClient, "/", UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE, &fd));
            CHECK(UmicomKernelVfsReadDirectory(&umicomClient, fd, &entry) == UMICOM_VFS_END); break;
        case 4:
            OK(UmicomKernelVfsCreate(&umicomClient, "/bank", UMICOM_VFS_DIRECTORY));
            OK(UmicomKernelVfsCreate(&umicomClient, "/bank/accounts", UMICOM_VFS_DIRECTORY));
            fd = UmicomFile("/bank/accounts/customer.txt");
            UmicomWriteFile(fd, 0U, 45U);
            CHECK(UmicomReadFile(fd, 0U, 100U) == 45U && !memcmp(umicomRead, umicomData, 45U)); break;
        case 5: {
            const char *bad[] = {"", "bank", "//bank", "/bank/", "/./bank", "/../bank", "/bank//x", "/bank/../x", "/bank\\x", "/bad\nname"};
            for (UmicomSize i = 0U; i < sizeof(bad)/sizeof(bad[0]); ++i) CHECK(UmicomKernelVfsCreate(&umicomClient, bad[i], UMICOM_VFS_FILE) == UMICOM_VFS_INVALID_PATH);
            char tooLong[UMICOM_VFS_PATH_BYTES + 1U];
            memset(tooLong, 'a', sizeof(tooLong)); tooLong[0] = '/'; tooLong[sizeof(tooLong)-1U] = '\0';
            CHECK(UmicomKernelVfsCreate(&umicomClient, tooLong, UMICOM_VFS_FILE) == UMICOM_VFS_INVALID_PATH);
            CHECK(umicomFs.nodes == 1U); break;
        }
        case 6: {
            char path[67]; path[0] = '/'; memset(path+1, 'a', 63U); path[64] = '\0';
            fd = UmicomFile(path); path[64] = 'b'; path[65] = '\0';
            CHECK(UmicomKernelVfsCreate(&umicomClient, path, UMICOM_VFS_FILE) == UMICOM_VFS_INVALID_PATH); break;
        }
        case 7: {
            char path[64] = "";
            for (unsigned i = 0U; i < 16U; ++i) { strcat(path, "/d"); OK(UmicomKernelVfsCreate(&umicomClient, path, UMICOM_VFS_DIRECTORY)); }
            strcat(path, "/d"); CHECK(UmicomKernelVfsCreate(&umicomClient, path, UMICOM_VFS_DIRECTORY) == UMICOM_VFS_INVALID_PATH); break;
        }
        case 8:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 17U);
            CHECK(UmicomKernelVfsCreate(&umicomClient, "/note", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_EXISTS);
            OK(UmicomKernelVfsQuery(&umicomClient, fd, &info)); CHECK(info.bytes == 17U); break;
        case 9:
            fd = UmicomFile("/note"); CHECK(UmicomKernelVfsCreate(&umicomClient, "/note/child", UMICOM_VFS_FILE) == UMICOM_VFS_NOT_DIRECTORY); break;
        case 10:
            OK(UmicomKernelVfsCreate(&umicomClient, "/folder", UMICOM_VFS_DIRECTORY)); fd = UmicomFile("/folder/item");
            CHECK(UmicomKernelVfsRemove(&umicomClient, "/folder", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_NOT_EMPTY); break;
        case 11:
            OK(UmicomKernelVfsCreate(&umicomClient, "/folder", UMICOM_VFS_DIRECTORY));
            OK(UmicomKernelVfsOpen(&umicomClient, "/folder", UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE, &fd));
            CHECK(UmicomKernelVfsRemove(&umicomClient, "/folder", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_BUSY);
            OK(UmicomKernelVfsClose(&umicomClient, fd)); OK(UmicomKernelVfsRemove(&umicomClient, "/folder", UMICOM_VFS_DIRECTORY)); break;
        case 12:
            fd = UmicomFile("/note");
            CHECK(UmicomKernelVfsOpen(&umicomClient, "/", RW, UMICOM_FALSE, &second) == UMICOM_VFS_NOT_FILE);
            CHECK(UmicomKernelVfsOpen(&umicomClient, "/note", UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE, &second) == UMICOM_VFS_NOT_DIRECTORY);
            CHECK(UmicomKernelVfsRemove(&umicomClient, "/note", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_NOT_DIRECTORY); break;
        case 13:
            fd = UmicomFile("/note");
            OK(UmicomKernelVfsClientOpen(&umicomOther, &umicomVfs, 202U, UMICOM_VFS_RIGHT_READ));
            CHECK(UmicomKernelVfsCreate(&umicomOther, "/bad", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
            CHECK(UmicomKernelVfsRemove(&umicomOther, "/note", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
            CHECK(UmicomKernelVfsOpen(&umicomOther, "/note", RW, UMICOM_FALSE, &second) == UMICOM_VFS_ACCESS_DENIED); break;
        case 14:
            fd = UmicomFile("/note");
            OK(UmicomKernelVfsOpen(&umicomClient, "/note", UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &second));
            CHECK(UmicomKernelVfsWrite(&umicomClient, second, umicomData, 1U, &count) == UMICOM_VFS_ACCESS_DENIED);
            CHECK(UmicomKernelVfsResize(&umicomClient, second, 5U) == UMICOM_VFS_ACCESS_DENIED);
            CHECK(UmicomKernelVfsQuery(&umicomClient, second, &info) == UMICOM_VFS_ACCESS_DENIED); break;
        case 15:
            fd = UmicomFile("/note");
            OK(UmicomKernelVfsDuplicate(&umicomClient, fd, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_DUPLICATE, &second));
            CHECK(UmicomKernelVfsDuplicate(&umicomClient, second, RW, &fd) == UMICOM_VFS_ACCESS_DENIED);
            OK(UmicomKernelVfsDuplicate(&umicomClient, second, 0U, &fd)); OK(UmicomKernelVfsClose(&umicomClient, fd)); break;
        case 16:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 32U); OK(UmicomKernelVfsSeek(&umicomClient, fd, 0U));
            OK(UmicomKernelVfsDuplicate(&umicomClient, fd, UMICOM_VFS_RIGHT_READ, &second));
            OK(UmicomKernelVfsRead(&umicomClient, fd, umicomRead, 4U, &count));
            OK(UmicomKernelVfsRead(&umicomClient, second, umicomRead, 3U, &count)); CHECK(!memcmp(umicomRead, umicomData+4, 3U)); break;
        case 17:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 32U);
            OK(UmicomKernelVfsOpen(&umicomClient, "/note", UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &second));
            OK(UmicomKernelVfsRead(&umicomClient, second, umicomRead, 3U, &count)); CHECK(!memcmp(umicomRead, umicomData, 3U)); break;
        case 18:
            fd = UmicomFile("/a"); UmicomWriteFile(fd, 0U, 32U);
            second = UmicomFile("/b"); UmicomWriteFile(second, 0U, 16U);
            OK(UmicomKernelVfsClientOpen(&umicomOther, &umicomVfs, 202U, RW));
            OK(UmicomKernelVfsOpen(&umicomOther, "/b", RW, UMICOM_FALSE, &second));
            CHECK(fd == second); /* Same number, deliberately different client-local objects. */
            OK(UmicomKernelVfsQuery(&umicomOther, second, &info)); CHECK(info.bytes == 16U);
            OK(UmicomKernelVfsQuery(&umicomClient, fd, &info)); CHECK(info.bytes == 32U); break;
        case 19:
            fd = UmicomFile("/note"); OK(UmicomKernelVfsClose(&umicomClient, fd));
            OK(UmicomKernelVfsOpen(&umicomClient, "/note", RW, UMICOM_FALSE, &second)); CHECK(fd != second);
            CHECK(UmicomKernelVfsClose(&umicomClient, fd) == UMICOM_VFS_INVALID_DESCRIPTOR); break;
        case 20:
            fd = UmicomFile("/note"); umicomClient.descriptors[0].generation = ~(UmicomU32)0U;
            fd = ((UmicomU64)(~(UmicomU32)0U) << 32U) | 1U;
            OK(UmicomKernelVfsClose(&umicomClient, fd)); CHECK(umicomClient.descriptors[0].retired);
            OK(UmicomKernelVfsOpen(&umicomClient, "/note", RW, UMICOM_FALSE, &second)); CHECK((UmicomU32)second == 2U); break;
        case 21:
            fd = UmicomFile("/note");
            for (unsigned i = 1U; i < UMICOM_VFS_DESCRIPTOR_LIMIT; ++i) OK(UmicomKernelVfsDuplicate(&umicomClient, fd, RW, &second));
            CHECK(UmicomFirstNode()->pins == 1U);
            CHECK(UmicomKernelVfsOpen(&umicomClient, "/note", RW, UMICOM_FALSE, &second) == UMICOM_VFS_CAPACITY);
            CHECK(UmicomKernelVfsDuplicate(&umicomClient, fd, RW, &second) == UMICOM_VFS_CAPACITY); break;
        case 22:
            for (unsigned i = 0U; i < UMICOM_VFS_CLIENT_LIMIT - 1U; ++i) OK(UmicomKernelVfsClientOpen(&umicomClients[i], &umicomVfs, i+2U, 0U));
            CHECK(UmicomKernelVfsClientOpen(&umicomOther, &umicomVfs, 99U, 0U) == UMICOM_VFS_CAPACITY); break;
        case 23:
            fd = UmicomFile("/note"); CHECK(UmicomReadFile(fd, 0U, 12U) == 0U); break;
        case 24:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 12U); CHECK(UmicomReadFile(fd, 7U, 99U) == 5U && !memcmp(umicomRead, umicomData+7U, 5U)); break;
        case 25:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 256U); CHECK(UmicomReadFile(fd, 0U, 256U) == 256U && !memcmp(umicomRead, umicomData, 256U)); break;
        case 26:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 4091U, 8197U); CHECK(UmicomReadFile(fd, 4091U, 8197U) == 8197U && !memcmp(umicomRead, umicomData, 8197U)); break;
        case 27: {
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 4096U);
            UmicomAddress spacer = 0U; CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&spacer) == UMICOM_KERNEL_MEMORY_OK);
            UmicomWriteFile(fd, 4096U, 4096U); CHECK(UmicomFirstNode()->pages[1] != UmicomFirstNode()->pages[0] + 4096U);
            CHECK(UmicomReadFile(fd, 4096U, 4096U) == 4096U && !memcmp(umicomRead, umicomData, 4096U));
            CHECK(UmicomKernelPhysicalMemoryFreeFrame(spacer) == UMICOM_KERNEL_MEMORY_OK); break;
        }
        case 28:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 8197U, 1U); CHECK(UmicomReadFile(fd, 0U, 8198U) == 8198U);
            for (unsigned i = 0U; i < 8197U; ++i) CHECK(umicomRead[i] == 0U);
            CHECK(umicomRead[8197] == umicomData[0] && umicomFs.dataPages == 1U); break;
        case 29:
            fd = UmicomFile("/note"); OK(UmicomKernelVfsResize(&umicomClient, fd, UMICOM_RAMFS_FILE_BYTES));
            CHECK(umicomFs.dataPages == 0U && UmicomReadFile(fd, 0U, sizeof(umicomRead)) == sizeof(umicomRead));
            for (unsigned i = 0U; i < sizeof(umicomRead); ++i) { CHECK(umicomRead[i] == 0U); }
            break;
        case 30:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 12000U); OK(UmicomKernelVfsResize(&umicomClient, fd, 4091U));
            OK(UmicomKernelVfsResize(&umicomClient, fd, 12000U)); CHECK(UmicomReadFile(fd, 0U, 12000U) == 12000U);
            CHECK(!memcmp(umicomRead, umicomData, 4091U)); for (unsigned i = 4091U; i < 12000U; ++i) CHECK(umicomRead[i] == 0U); break;
        case 31:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, 20U);
            OK(UmicomKernelVfsOpen(&umicomClient, "/note", RW, UMICOM_TRUE, &second)); OK(UmicomKernelVfsSeek(&umicomClient, second, 0U));
            OK(UmicomKernelVfsWrite(&umicomClient, second, umicomData, 7U, &count));
            CHECK(UmicomReadFile(fd, 20U, 7U) == 7U && !memcmp(umicomRead, umicomData, 7U)); break;
        case 32:
            fd = UmicomFile("/note"); UmicomWriteFile(fd, 0U, UMICOM_RAMFS_FILE_BYTES);
            CHECK(UmicomReadFile(fd, 0U, sizeof(umicomRead)) == sizeof(umicomRead) && !memcmp(umicomRead, umicomData, sizeof(umicomRead)));
            CHECK(UmicomKernelVfsWrite(&umicomClient, fd, umicomData, 1U, &count) == UMICOM_VFS_RANGE && count == 0U); break;
        case 33:
            fd = UmicomFile("/note"); umicomAllocateBudget = 1;
            CHECK(UmicomKernelVfsWrite(&umicomClient, fd, umicomData, 12288U, &count) == UMICOM_VFS_NO_MEMORY && count == 4096U);
            OK(UmicomKernelVfsQuery(&umicomClient, fd, &info)); CHECK(info.bytes == 4096U);
            umicomAllocateBudget = -1; OK(UmicomKernelVfsWrite(&umicomClient, fd, umicomData+4096U, 8192U, &count));
            CHECK(UmicomReadFile(fd, 0U, 12288U) == 12288U && !memcmp(umicomRead, umicomData, 12288U)); break;
        case 34:
            for (unsigned i = 0U; i < 4U; ++i) { char name[20]; snprintf(name, sizeof(name), "/file%u", i); fd=UmicomFile(name); UmicomWriteFile(fd, 0U, UMICOM_RAMFS_FILE_BYTES); }
            fd=UmicomFile("/last"); CHECK(UmicomKernelVfsWrite(&umicomClient, fd, umicomData, 1U, &count) == UMICOM_VFS_CAPACITY && count == 0U); break;
        case 35:
            for (unsigned i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) { char name[20]; snprintf(name,sizeof(name),"/file%u",i); OK(UmicomKernelVfsCreate(&umicomClient,name,UMICOM_VFS_FILE)); }
            CHECK(UmicomKernelVfsCreate(&umicomClient,"/full",UMICOM_VFS_FILE) == UMICOM_VFS_CAPACITY);
            OK(UmicomKernelVfsRemove(&umicomClient,"/file1",UMICOM_VFS_FILE)); OK(UmicomKernelRamfsReap(&umicomFs,&count)); CHECK(count==1U);
            OK(UmicomKernelVfsCreate(&umicomClient,"/full",UMICOM_VFS_FILE)); break;
        case 36:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,42U); OK(UmicomKernelVfsQuery(&umicomClient,fd,&info));
            OK(UmicomKernelVfsRemove(&umicomClient,"/note",UMICOM_VFS_FILE));
            CHECK(UmicomKernelVfsOpen(&umicomClient,"/note",RW,UMICOM_FALSE,&second)==UMICOM_VFS_NOT_FOUND);
            second=UmicomFile("/note"); CHECK(UmicomReadFile(second,0U,99U)==0U);
            CHECK(UmicomReadFile(fd,0U,99U)==42U && !memcmp(umicomRead,umicomData,42U)); break;
        case 37:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,8192U); OK(UmicomKernelVfsRemove(&umicomClient,"/note",UMICOM_VFS_FILE));
            OK(UmicomKernelRamfsReap(&umicomFs,&count)); CHECK(count==0U && umicomFs.dataPages==2U);
            OK(UmicomKernelVfsClose(&umicomClient,fd)); OK(UmicomKernelRamfsReap(&umicomFs,&count)); CHECK(count==1U && umicomFs.dataPages==0U); break;
        case 38:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,12288U); OK(UmicomKernelVfsRemove(&umicomClient,"/note",UMICOM_VFS_FILE)); OK(UmicomKernelVfsClose(&umicomClient,fd));
            umicomFreeCall=0U; umicomFailFree=2U; CHECK(UmicomKernelRamfsReap(&umicomFs,&count)==UMICOM_VFS_RELEASE_FAILED && count==0U);
            CHECK(umicomFs.dataPages==2U); OK(UmicomKernelRamfsValidate(&umicomFs));
            umicomFailFree=0U; OK(UmicomKernelRamfsReap(&umicomFs,&count)); CHECK(count==1U && umicomFs.dataPages==0U); break;
        case 39:
            for (unsigned failure=1U; failure<=4U; ++failure) {
                UmicomSetup(); fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,12288U);
                OK(UmicomKernelVfsClientClose(&umicomClient,&count)); OK(UmicomKernelVfsUnmount(&umicomVfs));
                umicomFreeCall=0U; umicomFailFree=failure; CHECK(UmicomKernelRamfsClose(&umicomFs)==UMICOM_VFS_RELEASE_FAILED);
                CHECK(umicomFs.state==UMICOM_VFS_CLOSING); umicomFailFree=0U; OK(UmicomKernelRamfsClose(&umicomFs)); CHECK(UmicomAllocated()==0U);
            } break;
        case 40:
            fd=UmicomFile("/note"); CHECK(UmicomKernelRamfsClose(&umicomFs)==UMICOM_VFS_BUSY);
            CHECK(UmicomKernelVfsUnmount(&umicomVfs)==UMICOM_VFS_BUSY); break;
        case 41:
            fd=UmicomFile("/note"); OK(UmicomKernelVfsDuplicate(&umicomClient,fd,RW,&second));
            CHECK(UmicomFirstNode()->pins==1U); OK(UmicomKernelVfsClientClose(&umicomClient,&count)); CHECK(count==2U && UmicomFirstNode()->pins==0U); break;
        case 42:
            fd=UmicomFile("/note"); OK(UmicomKernelVfsOpen(&umicomClient,"/",UMICOM_VFS_RIGHT_ENUMERATE,UMICOM_FALSE,&second));
            OK(UmicomKernelVfsReadDirectory(&umicomClient,second,&entry)); OK(UmicomKernelVfsCreate(&umicomClient,"/new",UMICOM_VFS_FILE));
            CHECK(UmicomKernelVfsReadDirectory(&umicomClient,second,&entry)==UMICOM_VFS_CHANGED);
            OK(UmicomKernelVfsRewindDirectory(&umicomClient,second)); OK(UmicomKernelVfsReadDirectory(&umicomClient,second,&entry)); break;
        case 43:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,19U); OK(UmicomKernelVfsOpen(&umicomClient,"/",UMICOM_VFS_RIGHT_ENUMERATE,UMICOM_FALSE,&second));
            OK(UmicomKernelVfsReadDirectory(&umicomClient,second,&entry)); CHECK(!strcmp(entry.name,"note") && entry.info.bytes==19U);
            OK(UmicomKernelVfsRemove(&umicomClient,"/note",UMICOM_VFS_FILE)); CHECK(entry.info.bytes==19U && !strcmp(entry.name,"note")); break;
        case 44:
            CHECK(UmicomKernelVfsRemove(&umicomClient,"/",UMICOM_VFS_DIRECTORY)==UMICOM_VFS_BUSY); break;
        case 45:
            CHECK(UmicomKernelRamfsInitialize(NULL)==UMICOM_VFS_INVALID_ARGUMENT);
            CHECK(UmicomKernelVfsOpen(&umicomClient,"/",0U,UMICOM_FALSE,NULL)==UMICOM_VFS_INVALID_ARGUMENT);
            CHECK(UmicomKernelRamfsReap(&umicomFs,NULL)==UMICOM_VFS_INVALID_ARGUMENT);
            CHECK(UmicomKernelVfsClientClose(&umicomClient,NULL)==UMICOM_VFS_INVALID_ARGUMENT); break;
        case 46:
            fd=UmicomFile("/note"); CHECK(UmicomKernelVfsSeek(&umicomClient,fd,~(UmicomSize)0U)==UMICOM_VFS_RANGE);
            CHECK(UmicomKernelVfsWrite(&umicomClient,fd,umicomData,~(UmicomSize)0U,&count)==UMICOM_VFS_RANGE && count==0U);
            CHECK(UmicomKernelVfsResize(&umicomClient,fd,~(UmicomSize)0U)==UMICOM_VFS_RANGE); break;
        case 47:
            fd=UmicomFile("/note"); OK(UmicomKernelVfsWrite(&umicomClient,fd,NULL,0U,&count)); CHECK(count==0U);
            OK(UmicomKernelVfsRead(&umicomClient,fd,NULL,0U,&count)); CHECK(count==0U && umicomFs.dataPages==0U); break;
        case 48:
            fd=UmicomFile("/note"); umicomAllowed=UMICOM_FALSE;
            CHECK(UmicomKernelVfsWrite(&umicomClient,fd,umicomData,1U,&count)==UMICOM_VFS_UNSAFE_CONTEXT);
            CHECK(UmicomKernelVfsClose(&umicomClient,fd)==UMICOM_VFS_UNSAFE_CONTEXT); umicomAllowed=UMICOM_TRUE; break;
        case 49:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,1U);
            CHECK(UmicomKernelPhysicalMemoryFreeFrame(UmicomFirstNode()->pages[0])==UMICOM_KERNEL_MEMORY_OK);
            CHECK(UmicomKernelRamfsValidate(&umicomFs)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 50:
            fd=UmicomFile("/note"); UmicomWriteFile(fd,0U,8192U);
            UmicomFirstNode()->pages[1]=UmicomFirstNode()->pages[0];
            CHECK(UmicomKernelRamfsValidate(&umicomFs)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 51:
            fd=UmicomFile("/note"); UmicomFirstNode()->parent=umicomFs.records[1].id;
            CHECK(UmicomKernelRamfsValidate(&umicomFs)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 52:
            fd=UmicomFile("/note"); ((UmicomU8 *)UmicomFirstNode())[sizeof(UmicomKernelRamfsNode)]^=1U;
            CHECK(UmicomKernelRamfsValidate(&umicomFs)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 53:
            fd=UmicomFile("/note"); umicomClient.descriptions[0].references=2U;
            CHECK(UmicomKernelVfsClientValidate(&umicomClient)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 54:
            fd=UmicomFile("/note"); umicomFs.epoch=~(UmicomU64)0U;
            CHECK(UmicomKernelVfsRemove(&umicomClient,"/note",UMICOM_VFS_FILE)==UMICOM_VFS_EXHAUSTED);
            CHECK(UmicomKernelVfsCreate(&umicomClient,"/new",UMICOM_VFS_FILE)==UMICOM_VFS_EXHAUSTED); break;
        case 55:
            umicomFs.lastIdentity=~(UmicomU64)0U;
            CHECK(UmicomKernelVfsCreate(&umicomClient,"/new",UMICOM_VFS_FILE)==UMICOM_VFS_EXHAUSTED); break;
        case 56: {
            UmicomKernelVfs otherMount={0}; OK(UmicomKernelVfsMount(&otherMount,UmicomKernelRamfsOperations(),&umicomFs));
            CHECK(umicomFs.root.pins==2U); OK(UmicomKernelVfsUnmount(&otherMount)); CHECK(umicomFs.root.pins==1U); break;
        }
        case 57:
            for (unsigned i=0U;i<2000U;++i) {
                fd=UmicomFile("/item"); UmicomWriteFile(fd,0U,31U);
                OK(UmicomKernelVfsRemove(&umicomClient,"/item",UMICOM_VFS_FILE)); OK(UmicomKernelVfsClose(&umicomClient,fd));
                OK(UmicomKernelRamfsReap(&umicomFs,&count)); CHECK(count==1U);
                CHECK(UmicomKernelVfsClose(&umicomClient,fd)==UMICOM_VFS_INVALID_DESCRIPTOR);
            } break;
        case 58: {
            fd=UmicomFile("/model"); UmicomSize length=0U; UmicomU32 random=0x12345678U;
            for (unsigned i=0U;i<4000U;++i) {
                random=random*1664525U+1013904223U;
                const UmicomSize position=(UmicomSize)(random%16000U);
                if ((i%5U)==0U) {
                    OK(UmicomKernelVfsResize(&umicomClient,fd,position));
                    if (position<length) memset(umicomModel+position,0,(size_t)(length-position));
                    length=position;
                } else {
                    UmicomWriteFile(fd,position,37U); memcpy(umicomModel+position,umicomData,37U);
                    if (position+37U>length) length=position+37U;
                }
                CHECK(UmicomReadFile(fd,0U,20000U)==length && !memcmp(umicomRead,umicomModel,(size_t)length));
            } break;
        }
        case 59:
            for (long budget=0;budget<=12;++budget) {
                UmicomSetup(); umicomAllocateBudget=budget;
                UmicomKernelVfsStatus status=UmicomKernelVfsCreate(&umicomClient,"/budget",UMICOM_VFS_FILE);
                if (status==UMICOM_VFS_OK) {
                    OK(UmicomKernelVfsOpen(&umicomClient,"/budget",RW,UMICOM_FALSE,&fd));
                    status=UmicomKernelVfsWrite(&umicomClient,fd,umicomData,40960U,&count);
                    CHECK(status==UMICOM_VFS_OK || status==UMICOM_VFS_NO_MEMORY);
                    CHECK(count==(UmicomSize)(budget>10?10:budget-1)*4096U);
                } else CHECK(status==UMICOM_VFS_NO_MEMORY);
                OK(UmicomKernelRamfsValidate(&umicomFs)); UmicomFinish();
            } break;
        case 60: {
            UmicomKernelVfs other={0}; UmicomKernelVfsOperations operations=*UmicomKernelRamfsOperations(); operations.write=NULL;
            CHECK(UmicomKernelVfsMount(&other,&operations,&umicomFs)==UMICOM_VFS_INVALID_ARGUMENT); CHECK(other.self==NULL); break;
        }
        case 61:
            second=0x1234U; CHECK(UmicomKernelVfsOpen(&umicomClient,"/missing",RW,UMICOM_FALSE,&second)==UMICOM_VFS_NOT_FOUND); CHECK(second==0x1234U);
            info.id=0x5678U; CHECK(UmicomKernelVfsQuery(&umicomClient,0U,&info)==UMICOM_VFS_INVALID_DESCRIPTOR); CHECK(info.id==0x5678U); break;
        case 62:
            fd=UmicomFile("/note"); UmicomFirstNode()->pages[0]=umicomFs.nodeCache.pages[0].frame; ++umicomFs.dataPages;
            CHECK(UmicomKernelRamfsValidate(&umicomFs)==UMICOM_VFS_CORRUPT_STATE); return 0;
        case 63:
            fd=UmicomFile("/note"); OK(UmicomKernelVfsClientClose(&umicomClient,&count)); OK(UmicomKernelVfsUnmount(&umicomVfs));
            umicomFreeCall=0U;umicomFailFree=1U;CHECK(UmicomKernelRamfsClose(&umicomFs)==UMICOM_VFS_RELEASE_FAILED);
            CHECK(umicomFs.nodes==1U && umicomFs.nodeCache.state==UMICOM_OBJECT_CACHE_CLOSING);
            umicomFailFree=0U;OK(UmicomKernelRamfsClose(&umicomFs));break;
        case 64:
            fd=UmicomFile("/first"); second=UmicomFile("/second");
            OK(UmicomKernelVfsOpen(&umicomClient,"/",UMICOM_VFS_RIGHT_ENUMERATE|UMICOM_VFS_RIGHT_DUPLICATE,UMICOM_FALSE,&fd));
            OK(UmicomKernelVfsDuplicate(&umicomClient,fd,UMICOM_VFS_RIGHT_ENUMERATE,&second));
            OK(UmicomKernelVfsReadDirectory(&umicomClient,fd,&entry)); CHECK(!strcmp(entry.name,"first"));
            OK(UmicomKernelVfsReadDirectory(&umicomClient,second,&entry)); CHECK(!strcmp(entry.name,"second"));break;
        default: return 2;
    }
    UmicomFinish();
    printf("PASS %s\n",argv[1]);
    return 0;
}
