/*-----------------------------------------------------------------------------
 * Umicom Kernel FAT16 provider compatibility and failure-path qualification.
 *
 * The reader supplies bytes from the existing synthetic disk. Every path,
 * cluster chain, VFS descriptor, copied result and provider pin is handled by
 * the production implementation. Failure injection changes transport outcomes,
 * never supplies an expected filesystem answer in place of that implementation.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_provider.h"
#include "../disk_inspection/fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

static UmicomU8 *image;
static UmicomKernelFat16Provider provider;
static UmicomKernelVfs vfs;
static UmicomKernelVfsClient client;
static UmicomKernelVfsClient secondClient;
static const UmicomKernelVfsOperations *ops;
static UmicomKernelDiskReader reader;
static UmicomU8 output[UMICOM_FAT16_READ_BYTES + 1U];
static UmicomSize reads, failRead, outside, policyCalls;
static UmicomKernelVfsStatus policyStatus = UMICOM_VFS_OK;
static UmicomKernelVfsStatus reentryStatus;
static UmicomBoolean policyReenter, readerReenter;

static UmicomKernelVfsStatus PolicyBegin(void *context)
{
    CHECK(context == &reader);
    ++policyCalls;
    if (policyReenter) {
        policyReenter = UMICOM_FALSE;
        reentryStatus = UmicomKernelFat16ProviderClose(&provider);
    }
    return policyStatus;
}
static UmicomBoolean SectorRead(void *context, UmicomU64 sector, UmicomU8 *target)
{
    CHECK(context == &reader);
    if (sector >= reader.sectors || sector >= UMICOM_DISK_FIXTURE_SECTORS) {
        ++outside; return UMICOM_FALSE;
    }
    ++reads;
    if (readerReenter) {
        readerReenter = UMICOM_FALSE;
        reentryStatus = UmicomKernelFat16ProviderClose(&provider);
    }
    if (reads == failRead) {
        memset(target, 0xdd, 512U);
        return UMICOM_FALSE;
    }
    memcpy(target, image + sector * 512U, 512U);
    return UMICOM_TRUE;
}
static void ProviderOpen(void)
{
    const UmicomKernelFat16ProviderIoPolicy policy = {PolicyBegin, &reader};
    CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, &policy) == UMICOM_VFS_OK);
}
static void ClientOpen(UmicomKernelVfsRights rights)
{
    ProviderOpen();
    CHECK(UmicomKernelVfsMount(&vfs, ops, &provider) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsClientOpen(&client, &vfs, 7U, rights) == UMICOM_VFS_OK);
}
static void Finish(void)
{
    UmicomSize closed = 0U;
    if (secondClient.state == UMICOM_VFS_OPEN)
        CHECK(UmicomKernelVfsClientClose(&secondClient, &closed) == UMICOM_VFS_OK);
    if (client.state == UMICOM_VFS_OPEN)
        CHECK(UmicomKernelVfsClientClose(&client, &closed) == UMICOM_VFS_OK);
    if (vfs.state == UMICOM_VFS_OPEN) CHECK(UmicomKernelVfsUnmount(&vfs) == UMICOM_VFS_OK);
    if (provider.state == UMICOM_VFS_OPEN)
        CHECK(UmicomKernelFat16ProviderClose(&provider) == UMICOM_VFS_OK);
    CHECK(provider.pins == 0U && outside == 0U);
}
static UmicomKernelFileDescriptor OpenFile(const char *path)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    CHECK(UmicomKernelVfsOpen(&client, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK);
    CHECK(descriptor != 0U);
    return descriptor;
}
static UmicomKernelFileDescriptor OpenDirectory(const char *path)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    CHECK(UmicomKernelVfsOpen(&client, path,
        UMICOM_VFS_RIGHT_ENUMERATE | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK);
    return descriptor;
}
static UmicomKernelVfsNodeId Lookup(UmicomKernelVfsNodeId parent, const char *name)
{
    UmicomKernelVfsNodeId node = 0U;
    CHECK(ops->lookup(&provider, parent, name, &node) == UMICOM_VFS_OK && node != 0U);
    return node;
}
static UmicomSize Position(UmicomKernelFileDescriptor descriptor)
{
    const UmicomSize slot = (UmicomSize)(UmicomU32)descriptor - 1U;
    CHECK(slot < UMICOM_VFS_DESCRIPTOR_LIMIT && client.descriptors[slot].occupied);
    return client.descriptions[client.descriptors[slot].description].position;
}
static void Sentinel(void) { memset(output, 0xa5, sizeof(output)); }
static void Unchanged(void)
{
    for (UmicomSize i = 0U; i < sizeof(output); ++i) CHECK(output[i] == 0xa5U);
}
static void Pattern(UmicomSize first, UmicomSize count)
{
    for (UmicomSize i = 0U; i < count; ++i)
        CHECK(output[i] == UmicomDiskFixturePattern(first + i));
}
static UmicomU8 *RootBytes(void) { return image + UMICOM_DISK_FIXTURE_ROOT * 512U; }
static void Entry(unsigned index, const char *name)
{
    UmicomU8 *entry = RootBytes() + index * 32U;
    memset(entry, 0, 32U);
    memcpy(entry, name, 11U);
    entry[11] = 0x20U; /* A distinct, valid empty file has no data cluster. */
}
static void Name(unsigned index, char name[13])
{
    CHECK(snprintf(name, 13U, "E%07u.TXT", index) == 12);
}
static void ManyNames(unsigned count)
{
    CHECK(count <= 128U);
    memset(RootBytes(), 0, 512U * 32U);
    for (unsigned i = 0U; i < count; ++i) {
        char raw[12];
        CHECK(snprintf(raw, sizeof(raw), "E%07uTXT", i) == 11);
        Entry(i, raw);
    }
}
static void ReadFailure(UmicomKernelFileDescriptor descriptor)
{
    Sentinel();
    UmicomSize count = 91U;
    CHECK(UmicomKernelVfsRead(&client, descriptor, output, 1300U, &count) == UMICOM_VFS_IO_ERROR);
    CHECK(count == 0U && Position(descriptor) == 0U);
    Unchanged();
    CHECK(!provider.busy && provider.pins == 2U);
}

static void Run(const char *test)
{
    if (!strcmp(test, "open_lifetime")) {
        ProviderOpen();
        CHECK(provider.self == &provider && provider.state == UMICOM_VFS_OPEN);
        CHECK(provider.pins == 0U && provider.volume.open && ops->validate(&provider) == UMICOM_VFS_OK);
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_BAD_STATE);
        CHECK(UmicomKernelFat16ProviderClose(&provider) == UMICOM_VFS_OK);
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_BAD_STATE);
        CHECK(ops->validate(&provider) == UMICOM_VFS_BAD_STATE);
    } else if (!strcmp(test, "null_policy")) {
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_OK);
        CHECK(Lookup(1U, "README.TXT") != 1U && policyCalls == 0U);
    } else if (!strcmp(test, "admission_retry")) {
        failRead = 1U;
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_IO_ERROR);
        CHECK(provider.lastDiskStatus == UMICOM_DISK_IO_ERROR && !provider.self && !provider.volume.open);
        failRead = 0U; ProviderOpen();
    } else if (!strcmp(test, "invalid_arguments")) {
        CHECK(UmicomKernelFat16ProviderOpen(0, &reader, 0U, 0) == UMICOM_VFS_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16ProviderOpen(&provider, 0, 0U, 0) == UMICOM_VFS_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 4U, 0) == UMICOM_VFS_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16ProviderClose(0) == UMICOM_VFS_INVALID_ARGUMENT);
        CHECK(reads == 0U);
    } else if (!strcmp(test, "case_identity")) {
        ProviderOpen();
        const UmicomKernelVfsNodeId upper = Lookup(1U, "README.TXT");
        CHECK(Lookup(1U, "readme.txt") == upper && Lookup(1U, "ReadMe.TxT") == upper);
    } else if (!strcmp(test, "distinct_empty_nodes")) {
        Entry(5U, "SECOND  TXT"); ProviderOpen();
        const UmicomKernelVfsNodeId a = Lookup(1U, "EMPTY.TXT"), b = Lookup(1U, "SECOND.TXT");
        CHECK(a != b && a != 1U && b != 1U);
        UmicomKernelVfsNodeInfo info = {0};
        CHECK(ops->stat(&provider, a, &info) == UMICOM_VFS_OK && info.bytes == 0U);
        CHECK(ops->stat(&provider, b, &info) == UMICOM_VFS_OK && info.bytes == 0U);
    } else if (!strcmp(test, "nested_lookup")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL);
        const UmicomKernelFileDescriptor descriptor = OpenFile("/docs/guide.txt");
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 4096U, &count) == UMICOM_VFS_OK);
        CHECK(count == sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
        CHECK(memcmp(output, UMICOM_DISK_FIXTURE_GUIDE, count) == 0);
    } else if (!strcmp(test, "fragmented_read")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        UmicomSize count = 0U; Sentinel();
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 4096U, &count) == UMICOM_VFS_OK && count == 1300U);
        Pattern(0U, count);
        for (UmicomSize i = count; i < sizeof(output); ++i) CHECK(output[i] == 0xa5U);
    } else if (!strcmp(test, "seek_duplicate")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        CHECK(UmicomKernelVfsSeek(&client, descriptor, 509U) == UMICOM_VFS_OK);
        UmicomKernelFileDescriptor duplicate = 0U;
        CHECK(UmicomKernelVfsDuplicate(&client, descriptor, UMICOM_VFS_RIGHT_READ, &duplicate) == UMICOM_VFS_OK);
        CHECK(provider.pins == 2U); UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&client, duplicate, output, 517U, &count) == UMICOM_VFS_OK && count == 517U);
        Pattern(509U, count); CHECK(Position(descriptor) == 1026U);
        CHECK(UmicomKernelVfsClose(&client, descriptor) == UMICOM_VFS_OK && provider.pins == 2U);
        CHECK(UmicomKernelVfsRead(&client, duplicate, output, 32U, &count) == UMICOM_VFS_OK && count == 32U);
        Pattern(1026U, count);
        CHECK(UmicomKernelVfsClose(&client, duplicate) == UMICOM_VFS_OK && provider.pins == 1U);
    } else if (!strcmp(test, "separate_positions")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor a = OpenFile("/FRAG.BIN"), b = OpenFile("/FRAG.BIN");
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&client, a, output, 17U, &count) == UMICOM_VFS_OK && count == 17U);
        CHECK(Position(a) == 17U && Position(b) == 0U && provider.pins == 3U);
        CHECK(UmicomKernelVfsRead(&client, b, output, 17U, &count) == UMICOM_VFS_OK); Pattern(0U, 17U);
    } else if (!strcmp(test, "eof_and_zero")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        const UmicomSize before = reads; UmicomSize count = 77U;
        CHECK(UmicomKernelVfsRead(&client, descriptor, 0, 0U, &count) == UMICOM_VFS_OK && count == 0U);
        CHECK(reads == before && Position(descriptor) == 0U);
        CHECK(UmicomKernelVfsSeek(&client, descriptor, 1299U) == UMICOM_VFS_OK); Sentinel();
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 16U, &count) == UMICOM_VFS_OK && count == 1U);
        Pattern(1299U, 1U); CHECK(output[1] == 0xa5U);
        Sentinel(); CHECK(UmicomKernelVfsRead(&client, descriptor, output, 16U, &count) == UMICOM_VFS_OK && count == 0U);
        Unchanged();
        const UmicomKernelFileDescriptor empty = OpenFile("/EMPTY.TXT");
        CHECK(UmicomKernelVfsRead(&client, empty, output, 16U, &count) == UMICOM_VFS_OK && count == 0U); Unchanged();
    } else if (!strcmp(test, "oversized_read")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        const UmicomSize before = reads; UmicomSize count = 99U; Sentinel();
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, sizeof(output), &count) == UMICOM_VFS_INSPECTION_LIMIT);
        CHECK(count == 0U && reads == before && Position(descriptor) == 0U); Unchanged();
    } else if (!strcmp(test, "failed_reads_atomic")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        reads = 0U; UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 1300U, &count) == UMICOM_VFS_OK && count == 1300U);
        const UmicomSize completeReads = reads; CHECK(completeReads > 3U);
        for (UmicomSize i = 1U; i <= completeReads; ++i) {
            CHECK(UmicomKernelVfsSeek(&client, descriptor, 0U) == UMICOM_VFS_OK);
            reads = 0U; failRead = i; ReadFailure(descriptor);
        }
        failRead = 0U;
    } else if (!strcmp(test, "directory_iteration")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenDirectory("/");
        const char *const names[] = {"README.TXT", "DOCS", "FRAG.BIN", "EMPTY.TXT"};
        UmicomKernelVfsDirectoryEntry entry;
        for (UmicomSize i = 0U; i < 4U; ++i) {
            CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_OK);
            CHECK(strcmp(entry.name, names[i]) == 0 && entry.info.id != 0U);
        }
        memset(&entry, 0xa5, sizeof(entry)); const UmicomKernelVfsDirectoryEntry before = entry;
        CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_END);
        CHECK(memcmp(&entry, &before, sizeof(entry)) == 0);
        CHECK(UmicomKernelVfsRewindDirectory(&client, descriptor) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_OK && !strcmp(entry.name, names[0]));
    } else if (!strcmp(test, "directory_epoch")) {
        ProviderOpen(); UmicomKernelVfsDirectoryEntry entry; memset(&entry, 0xa5, sizeof(entry));
        const UmicomKernelVfsDirectoryEntry before = entry; UmicomU64 epoch = 71U; UmicomSize next = 91U;
        CHECK(ops->enumerate(&provider, 1U, UMICOM_FAT16_PROVIDER_DIRECTORY_EPOCH + 1U,
            0U, &entry, &epoch, &next) == UMICOM_VFS_CHANGED);
        CHECK(epoch == 71U && next == 91U && memcmp(&entry, &before, sizeof(entry)) == 0);
    } else if (!strcmp(test, "directory_failure_cursor")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenDirectory("/");
        UmicomKernelVfsDirectoryEntry entry;
        CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_OK);
        const UmicomSize beforePosition = Position(descriptor);
        memset(&entry, 0xa5, sizeof(entry)); const UmicomKernelVfsDirectoryEntry before = entry;
        failRead = reads + 1U;
        CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_IO_ERROR);
        CHECK(Position(descriptor) == beforePosition && memcmp(&entry, &before, sizeof(entry)) == 0);
        failRead = 0U;
        CHECK(UmicomKernelVfsReadDirectory(&client, descriptor, &entry) == UMICOM_VFS_OK && !strcmp(entry.name, "DOCS"));
    } else if (!strcmp(test, "cache_eviction_stale_ids")) {
        ManyNames(96U); ProviderOpen(); UmicomKernelVfsNodeId ids[96];
        for (unsigned i = 0U; i < 96U; ++i) { char name[13]; Name(i, name); ids[i] = Lookup(1U, name); }
        UmicomSize stale = 0U;
        for (UmicomSize i = 0U; i < 96U; ++i) {
            UmicomKernelVfsNodeInfo info; memset(&info, 0xa5, sizeof(info));
            const UmicomKernelVfsNodeInfo before = info;
            const UmicomKernelVfsStatus status = ops->stat(&provider, ids[i], &info);
            if (status == UMICOM_VFS_NOT_FOUND) {
                ++stale; CHECK(memcmp(&info, &before, sizeof(info)) == 0);
                CHECK(ops->pin(&provider, ids[i]) == UMICOM_VFS_NOT_FOUND);
            } else CHECK(status == UMICOM_VFS_OK && info.id == ids[i]);
        }
        CHECK(stale >= 96U - (UMICOM_FAT16_PROVIDER_NODE_LIMIT - 1U));
        CHECK(ops->stat(&provider, 1U, &(UmicomKernelVfsNodeInfo){0}) == UMICOM_VFS_OK);
    } else if (!strcmp(test, "pinned_cache_survives")) {
        ManyNames(96U); ProviderOpen(); const UmicomKernelVfsNodeId kept = Lookup(1U, "E0000000.TXT");
        CHECK(ops->pin(&provider, kept) == UMICOM_VFS_OK);
        for (unsigned i = 1U; i < 96U; ++i) { char name[13]; Name(i, name); (void)Lookup(1U, name); }
        CHECK(Lookup(1U, "e0000000.txt") == kept);
        UmicomKernelVfsNodeInfo info = {0}; CHECK(ops->stat(&provider, kept, &info) == UMICOM_VFS_OK && info.id == kept);
        CHECK(ops->unpin(&provider, kept) == UMICOM_VFS_OK);
    } else if (!strcmp(test, "all_nodes_pinned_capacity")) {
        ManyNames(96U); ProviderOpen(); UmicomKernelVfsNodeId ids[UMICOM_FAT16_PROVIDER_NODE_LIMIT - 1U];
        for (unsigned i = 0U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT - 1U; ++i) {
            char name[13]; Name(i, name); ids[i] = Lookup(1U, name);
            CHECK(ops->pin(&provider, ids[i]) == UMICOM_VFS_OK);
        }
        UmicomKernelVfsNodeId refused = 99U;
        CHECK(ops->lookup(&provider, 1U, "E0000095.TXT", &refused) == UMICOM_VFS_CAPACITY && refused == 99U);
        CHECK(provider.pins == UMICOM_FAT16_PROVIDER_NODE_LIMIT - 1U);
        for (UmicomSize i = 0U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT - 1U; ++i)
            CHECK(ops->unpin(&provider, ids[i]) == UMICOM_VFS_OK);
        CHECK(Lookup(1U, "E0000095.TXT") != 0U);
    } else if (!strcmp(test, "node_identity_exhaustion")) {
        ProviderOpen(); provider.nextNodeId = ~(UmicomKernelVfsNodeId)0U;
        const UmicomKernelVfsNodeId last = Lookup(1U, "README.TXT"); CHECK(last == ~(UmicomKernelVfsNodeId)0U);
        CHECK(provider.nextNodeId == 0U && Lookup(1U, "readme.txt") == last);
        UmicomKernelVfsNodeId refused = 77U;
        CHECK(ops->lookup(&provider, 1U, "EMPTY.TXT", &refused) == UMICOM_VFS_EXHAUSTED && refused == 77U);
    } else if (!strcmp(test, "provider_busy_close")) {
        ProviderOpen(); const UmicomKernelVfsNodeId id = Lookup(1U, "README.TXT");
        CHECK(ops->pin(&provider, id) == UMICOM_VFS_OK);
        CHECK(UmicomKernelFat16ProviderClose(&provider) == UMICOM_VFS_BUSY);
        CHECK(provider.state == UMICOM_VFS_OPEN && provider.volume.open && provider.pins == 1U);
        CHECK(ops->unpin(&provider, id) == UMICOM_VFS_OK);
    } else if (!strcmp(test, "vfs_busy_unmount")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/README.TXT");
        CHECK(UmicomKernelVfsUnmount(&vfs) == UMICOM_VFS_BUSY);
        CHECK(UmicomKernelFat16ProviderClose(&provider) == UMICOM_VFS_BUSY);
        CHECK(vfs.state == UMICOM_VFS_OPEN && provider.pins == 2U && Position(descriptor) == 0U);
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 17U, &count) == UMICOM_VFS_OK && count == 17U);
    } else if (!strcmp(test, "empty_client_unmount")) {
        ClientOpen(0U);
        CHECK(provider.pins == 1U && vfs.clients == 1U);
        CHECK(UmicomKernelVfsUnmount(&vfs) == UMICOM_VFS_BUSY && vfs.state == UMICOM_VFS_OPEN);
    } else if (!strcmp(test, "copied_owner")) {
        ProviderOpen(); UmicomKernelFat16Provider *copy = malloc(sizeof(*copy)); CHECK(copy);
        memcpy(copy, &provider, sizeof(*copy));
        CHECK(ops->validate(copy) == UMICOM_VFS_BAD_STATE);
        CHECK(UmicomKernelFat16ProviderClose(copy) == UMICOM_VFS_BAD_STATE); free(copy);
        CHECK(ops->validate(&provider) == UMICOM_VFS_OK);
    } else if (!strcmp(test, "malformed_media")) {
        image[510] = 0U;
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_CORRUPT_FILESYSTEM);
        CHECK(provider.lastDiskStatus == UMICOM_DISK_SIGNATURE && !provider.self);
    } else if (!strcmp(test, "unsupported_media")) {
        image[450] = 0xeeU;
        CHECK(UmicomKernelFat16ProviderOpen(&provider, &reader, 0U, 0) == UMICOM_VFS_UNSUPPORTED);
        CHECK(provider.lastDiskStatus == UMICOM_DISK_UNSUPPORTED_TABLE && !provider.self);
    } else if (!strcmp(test, "path_refusals")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL);
        const char *const paths[] = {"README.TXT", "/README.TXT/", "//README.TXT", "/DOCS/../README.TXT", "/./README.TXT", "/DOCS\\GUIDE.TXT", "/TOOLONGNAME.TXT"};
        for (UmicomSize i = 0U; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            UmicomKernelFileDescriptor descriptor = 99U;
            CHECK(UmicomKernelVfsOpen(&client, paths[i], UMICOM_VFS_RIGHT_READ,
                UMICOM_FALSE, &descriptor) == UMICOM_VFS_INVALID_PATH && descriptor == 99U);
        }
        UmicomKernelFileDescriptor descriptor = 99U;
        CHECK(UmicomKernelVfsOpen(&client, "/MISSING.TXT", UMICOM_VFS_RIGHT_READ,
            UMICOM_FALSE, &descriptor) == UMICOM_VFS_NOT_FOUND && descriptor == 99U);
        CHECK(provider.pins == 1U);
    } else if (!strcmp(test, "policy_reentry")) {
        ProviderOpen(); policyReenter = UMICOM_TRUE;
        CHECK(Lookup(1U, "README.TXT") != 1U && reentryStatus == UMICOM_VFS_BUSY);
        CHECK(!provider.busy && provider.state == UMICOM_VFS_OPEN);
    } else if (!strcmp(test, "policy_refusal")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); const UmicomKernelFileDescriptor descriptor = OpenFile("/FRAG.BIN");
        policyStatus = UMICOM_VFS_UNSAFE_CONTEXT; const UmicomSize before = reads;
        Sentinel(); UmicomSize count = 91U;
        CHECK(UmicomKernelVfsRead(&client, descriptor, output, 32U, &count) == UMICOM_VFS_UNSAFE_CONTEXT);
        CHECK(count == 0U && reads == before && Position(descriptor) == 0U); Unchanged();
        const UmicomSize calls = policyCalls; UmicomKernelVfsNodeInfo info = {0};
        CHECK(UmicomKernelVfsQuery(&client, descriptor, &info) == UMICOM_VFS_OK && info.bytes == 1300U);
        CHECK(UmicomKernelVfsClose(&client, descriptor) == UMICOM_VFS_OK && policyCalls == calls);
        Finish(); CHECK(policyCalls == calls);
    } else if (!strcmp(test, "reader_reentry")) {
        ProviderOpen(); readerReenter = UMICOM_TRUE;
        CHECK(Lookup(1U, "README.TXT") != 1U && reentryStatus == UMICOM_VFS_BUSY);
        CHECK(!provider.busy);
    } else if (!strcmp(test, "mutation_callbacks")) {
        ProviderOpen(); const UmicomKernelVfsNodeId node = Lookup(1U, "README.TXT");
        const UmicomSize before = reads; UmicomSize written = 99U;
        CHECK(ops->create(&provider, 1U, "NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_READ_ONLY);
        CHECK(ops->unlink(&provider, 1U, "README.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_READ_ONLY);
        CHECK(ops->write(&provider, node, 0U, "x", 1U, &written) == UMICOM_VFS_READ_ONLY && written == 0U);
        CHECK(ops->resize(&provider, node, 0U) == UMICOM_VFS_READ_ONLY && reads == before);
    } else if (!strcmp(test, "broad_client_mutations")) {
        ClientOpen(UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor descriptor = 0U;
        CHECK(UmicomKernelVfsOpen(&client, "/README.TXT", UMICOM_VFS_RIGHT_WRITE,
            UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK);
        UmicomSize written = 99U;
        CHECK(UmicomKernelVfsWrite(&client, descriptor, "x", 1U, &written) == UMICOM_VFS_READ_ONLY && written == 0U);
        CHECK(UmicomKernelVfsResize(&client, descriptor, 0U) == UMICOM_VFS_READ_ONLY);
        CHECK(UmicomKernelVfsCreate(&client, "/NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_READ_ONLY);
        CHECK(UmicomKernelVfsRemove(&client, "/README.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_READ_ONLY);
        CHECK(Position(descriptor) == 0U);
    } else if (!strcmp(test, "status_names")) {
        CHECK(UMICOM_VFS_OK == 0 && UMICOM_VFS_CORRUPT_STATE == 20);
        CHECK(!strcmp(UmicomKernelVfsStatusName(UMICOM_VFS_IO_ERROR), "io-error"));
        CHECK(!strcmp(UmicomKernelVfsStatusName(UMICOM_VFS_READ_ONLY), "read-only"));
        CHECK(!strcmp(UmicomKernelVfsStatusName(UMICOM_VFS_UNSUPPORTED), "unsupported"));
        CHECK(!strcmp(UmicomKernelVfsStatusName(UMICOM_VFS_INSPECTION_LIMIT), "inspection-limit"));
        CHECK(!strcmp(UmicomKernelVfsStatusName(UMICOM_VFS_CORRUPT_FILESYSTEM), "corrupt-filesystem"));
    } else { fprintf(stderr, "unknown provider case: %s\n", test); exit(2); }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const UmicomSize bytes = (UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    image = malloc(bytes); CHECK(image);
    FILE *fixture = fopen(argv[2], "rb"); CHECK(fixture);
    CHECK(fread(image, 1U, bytes, fixture) == bytes && fgetc(fixture) == EOF);
    CHECK(fclose(fixture) == 0);
    ops = UmicomKernelFat16ProviderOperationsGet(); CHECK(ops);
    reader = (UmicomKernelDiskReader){UMICOM_DISK_FIXTURE_SECTORS, SectorRead, &reader};
    Run(argv[1]); Finish(); free(image); return 0;
}
