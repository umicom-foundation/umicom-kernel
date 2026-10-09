/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/disk_writable_filesystem/writable_tests.c
 *
 * Use the established independent visible/durable media and real VirtIO DMA
 * model, then drive the production writable mount through VFS descriptors.
 * Existing harness entry points remain intact; --wrap=main selects this suite.
 * These are native C qualification tests, not emulated RISC-V execution.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "../fat16_commit/commit_tests.c"
#include "umicom/kernel/disk_writable_filesystem_console.h"

#define UMICOM_KERNEL_WRITABLE_TEST_RIGHTS (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE | \
    UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE)
static UmicomKernelDiskWritableMount umicomKernelTestWritableMount;
static UmicomKernelDiskWritableMount umicomKernelTestOtherMount;
static UmicomKernelVfsClient umicomKernelTestWritableClient;
static UmicomKernelVfsClient umicomKernelTestOtherClient;
static UmicomKernelFat16FileTime umicomKernelTestWritableTime = {2044U, 2U, 29U, 23U, 58U, 57U};
static UmicomU8 umicomKernelTestWritableOutput[4097];
static UmicomU8 umicomKernelTestWritableBefore[4097];
static UmicomKernelConsoleShell umicomKernelTestWritableShell;
static UmicomBoolean umicomKernelTestWritableReentry;
static UmicomU32 umicomKernelTestWritableReentries;
static UmicomBoolean umicomKernelTestWritableArgumentReentry;

static const UmicomKernelVfsOperations *UmicomKernelWritableTestOperations(void)
{
    return UmicomKernelFat16WritableProviderOperationsGet();
}
static void UmicomKernelWritableTestOpen(void)
{
    CHECK(UmicomKernelDiskWritableMountOpen(&umicomKernelTestWritableMount, &domain,
        0U, 0U, 32U, &umicomKernelTestWritableTime) == UMICOM_VFS_OK);
    CHECK(UmicomKernelDiskWritableMountClientOpen(&umicomKernelTestWritableMount,
        &umicomKernelTestWritableClient, 73U, UMICOM_VFS_RIGHT_ALL) == UMICOM_VFS_OK);
    CHECK(Allocated() == 2U && !commitWrites && !commitFlushes);
}
static UmicomKernelFileDescriptor UmicomKernelWritableTestDescriptor(const char *path,
    UmicomKernelVfsRights rights, UmicomBoolean append)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    CHECK(UmicomKernelVfsOpen(&umicomKernelTestWritableClient, path, rights, append, &descriptor) == UMICOM_VFS_OK);
    CHECK(descriptor);
    return descriptor;
}
static UmicomKernelVfsNodeInfo UmicomKernelWritableTestInfo(UmicomKernelFileDescriptor descriptor)
{
    UmicomKernelVfsNodeInfo info;
    CHECK(UmicomKernelVfsQuery(&umicomKernelTestWritableClient, descriptor, &info) == UMICOM_VFS_OK);
    return info;
}
static void UmicomKernelWritableTestClose(void)
{
    model.allowed = UMICOM_TRUE; model.noCompletion = UMICOM_FALSE;
    const UmicomU32 mutations = commitMutations;
    UmicomSize closed = 0U;
    if (umicomKernelTestOtherClient.self && umicomKernelTestOtherClient.state != UMICOM_VFS_CLOSED)
        CHECK(UmicomKernelVfsClientClose(&umicomKernelTestOtherClient, &closed) == UMICOM_VFS_OK);
    if (umicomKernelTestWritableClient.self && umicomKernelTestWritableClient.state != UMICOM_VFS_CLOSED)
        CHECK(UmicomKernelVfsClientClose(&umicomKernelTestWritableClient, &closed) == UMICOM_VFS_OK);
    CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestWritableMount) == UMICOM_VFS_OK);
    CHECK(!Allocated() && mutations == commitMutations);
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static void UmicomKernelWritableTestAccepted(UmicomU64 count)
{
    CHECK(umicomKernelTestWritableMount.lifecycle.committedOperations == count);
    CHECK(umicomKernelTestWritableMount.provider.committedOperations == count);
    CHECK(umicomKernelTestWritableMount.lifecycle.lastResult.commit.commitAccepted);
    CHECK(umicomKernelTestWritableMount.lifecycle.lastResult.commit.cleanVerified);
    CommitEqual(commitVisible, commitDurable, sizeof(commitVisible));
}
static void UmicomKernelWritableTestFresh(const char *path, const UmicomU8 *expected, UmicomSize bytes)
{
    UmicomKernelDiskMount reader = {0};
    UmicomKernelVfsClient client = {0};
    model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
    model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
    model.expectedDriverLow = 0U;
    const UmicomU32 mutations = commitMutations;
    CHECK(UmicomKernelDiskMountOpen(&reader, &domain, 0U, 0U, 32U) == UMICOM_VFS_OK);
    CHECK(UmicomKernelDiskMountClientOpen(&reader, &client, 83U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK);
    UmicomKernelFileDescriptor descriptor = 0U; UmicomSize read = 0U;
    CHECK(UmicomKernelVfsOpen(&client, path, UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsRead(&client, descriptor, umicomKernelTestWritableOutput, bytes, &read) == UMICOM_VFS_OK);
    CHECK(read == bytes); CommitEqual(umicomKernelTestWritableOutput, expected, bytes);
    CHECK(UmicomKernelVfsClientClose(&client, &read) == UMICOM_VFS_OK);
    CHECK(UmicomKernelDiskMountClose(&reader) == UMICOM_VFS_OK);
    CHECK(!Allocated() && mutations == commitMutations);
}
static void UmicomKernelWritableTestMount(const char *name)
{
    if (!strcmp(name, "bad-calendar")) {
        UmicomKernelFat16FileTime invalid = {2100U, 2U, 29U, 0U, 0U, 0U};
        CHECK(UmicomKernelDiskWritableMountOpen(&umicomKernelTestWritableMount, &domain,
            0U, 0U, 32U, &invalid) == UMICOM_VFS_INVALID_ARGUMENT);
        CHECK(!Allocated() && !commitEventCount && !umicomKernelTestWritableMount.self);
    } else if (!strcmp(name, "readonly") || !strcmp(name, "no-flush") || !strcmp(name, "failed-admission")) {
        if (!strcmp(name, "readonly")) model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        else if (!strcmp(name, "no-flush")) model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
        else { commitVisible[(UmicomSize)COMMIT_PRIMARY * 512U + 3U] &= 0x7fU; CommitRebase(); }
        CHECK(UmicomKernelDiskWritableMountOpen(&umicomKernelTestWritableMount, &domain,
            0U, 0U, 32U, &umicomKernelTestWritableTime) != UMICOM_VFS_OK);
        CHECK(!commitWrites && !commitFlushes);
        CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestWritableMount) == UMICOM_VFS_OK);
        CHECK(!Allocated()); return;
    }
    UmicomKernelWritableTestOpen();
    if (!strcmp(name, "exclusive")) {
        CHECK(UmicomKernelDiskWritableMountOpen(&umicomKernelTestOtherMount, &domain,
            0U, 0U, 32U, &umicomKernelTestWritableTime) != UMICOM_VFS_OK);
        CHECK(Allocated() == 2U);
        CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestOtherMount) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "reset-retry") || !strcmp(name, "free-retry")) {
        UmicomSize closed = 0U;
        CHECK(UmicomKernelVfsClientClose(&umicomKernelTestWritableClient, &closed) == UMICOM_VFS_OK);
        if (!strcmp(name, "reset-retry")) model.stuckReset = UMICOM_TRUE;
        else model.failFree = 1U;
        CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestWritableMount) != UMICOM_VFS_OK);
        CHECK(umicomKernelTestWritableMount.state == UMICOM_VFS_CLOSING && Allocated());
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    } else {
        CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestWritableMount) == UMICOM_VFS_BUSY);
        CHECK(umicomKernelTestWritableMount.state == UMICOM_VFS_OPEN);
    }
    UmicomKernelWritableTestClose();
    CHECK(UmicomKernelDiskWritableMountOpen(&umicomKernelTestWritableMount, &domain,
        0U, 0U, 32U, &umicomKernelTestWritableTime) == UMICOM_VFS_BAD_STATE);
}
static void UmicomKernelWritableTestWrite(const char *name)
{
    if (!strcmp(name, "readonly")) { CommitRoot(3U)[11] |= 1U; CommitRebase(); }
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/FRAG.BIN",
        UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
    const UmicomKernelVfsNodeInfo original = UmicomKernelWritableTestInfo(descriptor);
    UmicomSize written = 99U;
    if (!strcmp(name, "hole") || !strcmp(name, "too-large") || !strcmp(name, "readonly")) {
        if (!strcmp(name, "hole")) CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, descriptor, 1301U) == UMICOM_VFS_OK);
        const UmicomKernelVfsStatus status = UmicomKernelVfsWrite(&umicomKernelTestWritableClient,
            descriptor, commitInput, !strcmp(name, "too-large") ? 4097U : 1U, &written);
        CHECK(status == (!strcmp(name, "hole") ? UMICOM_VFS_RANGE :
            !strcmp(name, "readonly") ? UMICOM_VFS_READ_ONLY : UMICOM_VFS_INSPECTION_LIMIT));
        CHECK(!written && !commitWrites && !commitFlushes);
        UmicomKernelWritableTestClose(); return;
    }
    if (!strcmp(name, "zero")) {
        const UmicomU32 reads = commitReads;
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, NULL, 0U, &written) == UMICOM_VFS_OK);
        CHECK(!written && !commitWrites && !commitFlushes && reads == commitReads);
        CHECK(!umicomKernelTestWritableMount.lifecycle.committedOperations);
        UmicomKernelWritableTestClose(); return;
    }
    if (!strcmp(name, "append-interleaved")) {
        const UmicomKernelFileDescriptor first = UmicomKernelWritableTestDescriptor("/EMPTY.TXT",
            UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_TRUE);
        const UmicomKernelFileDescriptor second = UmicomKernelWritableTestDescriptor("/empty.txt",
            UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_TRUE);
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, first, "one", 3U, &written) == UMICOM_VFS_OK && written == 3U);
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, second, "two", 3U, &written) == UMICOM_VFS_OK && written == 3U);
        CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, first, 0U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, first, "!", 1U, &written) == UMICOM_VFS_OK && written == 1U);
        CHECK(UmicomKernelWritableTestInfo(first).bytes == 7U && UmicomKernelWritableTestInfo(second).bytes == 7U);
        UmicomKernelWritableTestAccepted(3U); UmicomKernelWritableTestClose();
        UmicomKernelWritableTestFresh("/EMPTY.TXT", (const UmicomU8 *)"onetwo!", 7U); return;
    }
    const UmicomSize offset = !strcmp(name, "overwrite") ? 511U : !strcmp(name, "extend") ? 1200U : 0U;
    const UmicomSize bytes = !strcmp(name, "overwrite") ? 700U : !strcmp(name, "extend") ? 600U : 4096U;
    CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, descriptor, offset) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, commitInput, bytes, &written) == UMICOM_VFS_OK);
    CHECK(written == bytes); UmicomKernelWritableTestAccepted(1U);
    const UmicomKernelVfsNodeInfo changed = UmicomKernelWritableTestInfo(descriptor);
    CHECK(changed.id == original.id && changed.bytes == (offset + bytes > 1300U ? offset + bytes : 1300U));
    CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, descriptor, offset) == UMICOM_VFS_OK);
    UmicomSize read = 0U;
    CHECK(UmicomKernelVfsRead(&umicomKernelTestWritableClient, descriptor, umicomKernelTestWritableOutput,
        bytes, &read) == UMICOM_VFS_OK && read == bytes);
    CommitEqual(umicomKernelTestWritableOutput, commitInput, bytes);
    if (!strcmp(name, "overwrite")) {
        /* Independent entire-image oracle: only the requested fragmented file
         * bytes plus ARCHIVE/write calendar may differ from the original. */
        CommitPatchExpected(offset, bytes);
        UmicomU8 *const raw = commitExpected + (UmicomSize)UMICOM_DISK_FIXTURE_ROOT * 512U + 96U;
        raw[11] |= 0x20U; CommitPut16(raw + 22U, 0xbf5cU); CommitPut16(raw + 24U, 0x805dU);
        CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    }
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestResize(const char *name)
{
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/FRAG.BIN",
        UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
    if (!strcmp(name, "bound")) {
        CHECK(UmicomKernelVfsResize(&umicomKernelTestWritableClient, descriptor, 1300U + 4097U) == UMICOM_VFS_INSPECTION_LIMIT);
        CHECK(!commitWrites && !commitFlushes);
    } else if (!strcmp(name, "shrink")) {
        CHECK(UmicomKernelVfsResize(&umicomKernelTestWritableClient, descriptor, 512U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelWritableTestInfo(descriptor).bytes == 512U);
        CHECK(umicomKernelTestWritableMount.lifecycle.lastResult.freedClusters == 2U);
        CHECK(UmicomKernelVfsResize(&umicomKernelTestWritableClient, descriptor, 0U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelWritableTestInfo(descriptor).bytes == 0U);
        UmicomKernelWritableTestAccepted(2U);
    } else {
        CHECK(UmicomKernelVfsResize(&umicomKernelTestWritableClient, descriptor, 2000U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelWritableTestInfo(descriptor).bytes == 2000U);
        CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, descriptor, 1300U) == UMICOM_VFS_OK);
        memset(umicomKernelTestWritableOutput, 0xa5, sizeof(umicomKernelTestWritableOutput));
        UmicomSize read = 0U;
        CHECK(UmicomKernelVfsRead(&umicomKernelTestWritableClient, descriptor,
            umicomKernelTestWritableOutput, 700U, &read) == UMICOM_VFS_OK && read == 700U);
        for (UmicomSize i = 0U; i < 700U; ++i) CHECK(!umicomKernelTestWritableOutput[i]);
        const UmicomU32 reads = commitReads, mutations = commitMutations;
        CHECK(UmicomKernelVfsResize(&umicomKernelTestWritableClient, descriptor, 2000U) == UMICOM_VFS_OK);
        CHECK(reads == commitReads && mutations == commitMutations);
        UmicomKernelWritableTestAccepted(1U);
    }
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestRights(const char *name)
{
    UmicomKernelWritableTestOpen();
    UmicomSize written = 97U;
    if (!strcmp(name, "read-only")) {
        CHECK(UmicomKernelDiskWritableMountClientOpen(&umicomKernelTestWritableMount,
            &umicomKernelTestOtherClient, 17U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK);
        UmicomKernelFileDescriptor descriptor = 0U;
        const UmicomU32 reads = commitReads;
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestOtherClient, "/NO.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestOtherClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(UmicomKernelVfsOpen(&umicomKernelTestOtherClient, "/FRAG.BIN", UMICOM_VFS_RIGHT_WRITE,
            UMICOM_FALSE, &descriptor) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(reads == commitReads && !commitWrites && !commitFlushes);
    } else {
        const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/FRAG.BIN",
            UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
        UmicomKernelFileDescriptor duplicate = 0U;
        CHECK(UmicomKernelVfsDuplicate(&umicomKernelTestWritableClient, descriptor,
            UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, &duplicate) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, duplicate, "x", 1U, &written) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(written == 97U && !commitWrites && !commitFlushes);
        CHECK(UmicomKernelVfsSeek(&umicomKernelTestWritableClient, duplicate, 511U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, "x", 1U, &written) == UMICOM_VFS_OK && written == 1U);
        CHECK(UmicomKernelVfsRead(&umicomKernelTestWritableClient, duplicate, umicomKernelTestWritableOutput,
            1U, &written) == UMICOM_VFS_OK && written == 1U);
        CHECK(umicomKernelTestWritableOutput[0] == commitInitial[CommitFileAddress(512U)]);
    }
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestCacheFixture(void)
{
    for (UmicomSize i = 5U; i < 100U; ++i) {
        char alias[12]; CHECK(snprintf(alias, sizeof(alias), "E%07uTXT", (unsigned)i) == 11);
        CommitEntry(CommitRoot(i), alias, 0x20U, 0U, 0U);
    }
    CommitRoot(100U)[0] = 0U; CommitRebase();
}
static void UmicomKernelWritableTestIdentity(const char *name)
{
    if (!strcmp(name, "pinned-cache") || !strcmp(name, "all-pinned")) UmicomKernelWritableTestCacheFixture();
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/empty.txt",
        UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
    const UmicomKernelVfsNodeInfo before = UmicomKernelWritableTestInfo(descriptor);
    if (!strcmp(name, "case")) {
        const UmicomKernelFileDescriptor second = UmicomKernelWritableTestDescriptor("/EMPTY.TXT",
            UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
        CHECK(UmicomKernelWritableTestInfo(second).id == before.id);
        UmicomSize written = 0U;
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, "ok", 2U, &written) == UMICOM_VFS_OK && written == 2U);
        CHECK(UmicomKernelWritableTestInfo(second).id == before.id && UmicomKernelWritableTestInfo(second).bytes == 2U);
    } else if (!strcmp(name, "reuse")) {
        CHECK(UmicomKernelVfsClose(&umicomKernelTestWritableClient, descriptor) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
        const UmicomKernelFileDescriptor replacement = UmicomKernelWritableTestDescriptor("/EMPTY.TXT", UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE);
        CHECK(UmicomKernelWritableTestInfo(replacement).id != before.id);
        CHECK(UmicomKernelVfsQuery(&umicomKernelTestWritableClient, descriptor,
            (UmicomKernelVfsNodeInfo *)umicomKernelTestWritableOutput) == UMICOM_VFS_INVALID_DESCRIPTOR);
    } else if (!strcmp(name, "exhausted")) {
        umicomKernelTestWritableMount.provider.nextNodeId = 0U;
        UmicomKernelFileDescriptor rejected = 99U;
        CHECK(UmicomKernelVfsOpen(&umicomKernelTestWritableClient, "/FRAG.BIN", UMICOM_VFS_RIGHT_READ,
            UMICOM_FALSE, &rejected) == UMICOM_VFS_EXHAUSTED && rejected == 99U);
    } else {
        UmicomKernelVfsNodeId pinned[64]; UmicomSize count = 0U;
        for (UmicomSize i = 5U; i < 100U; ++i) {
            char alias[13]; CHECK(snprintf(alias, sizeof(alias), "E%07u.TXT", (unsigned)i) == 12);
            UmicomKernelVfsNodeId node = 0U;
            const UmicomKernelVfsStatus status = UmicomKernelWritableTestOperations()->lookup(
                &umicomKernelTestWritableMount.provider, 1U, alias, &node);
            if (!strcmp(name, "all-pinned") && count == 62U) { CHECK(status == UMICOM_VFS_CAPACITY); break; }
            CHECK(status == UMICOM_VFS_OK);
            if (!strcmp(name, "all-pinned")) {
                CHECK(UmicomKernelWritableTestOperations()->pin(&umicomKernelTestWritableMount.provider, node) == UMICOM_VFS_OK);
                pinned[count++] = node;
            }
        }
        CHECK(UmicomKernelWritableTestInfo(descriptor).id == before.id);
        for (UmicomSize i = 0U; i < count; ++i)
            CHECK(UmicomKernelWritableTestOperations()->unpin(&umicomKernelTestWritableMount.provider, pinned[i]) == UMICOM_VFS_OK);
    }
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestDirectory(const char *name)
{
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor directory = UmicomKernelWritableTestDescriptor("/",
        UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE);
    UmicomKernelVfsDirectoryEntry entry, before;
    CHECK(UmicomKernelVfsReadDirectory(&umicomKernelTestWritableClient, directory, &entry) == UMICOM_VFS_OK);
    const UmicomU64 epoch = umicomKernelTestWritableMount.provider.directoryEpoch;
    const UmicomSize cursor = umicomKernelTestWritableClient.descriptions[0].position;
    if (!strcmp(name, "changed")) {
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
        memset(&entry, 0xa5, sizeof(entry)); before = entry;
        CHECK(UmicomKernelVfsReadDirectory(&umicomKernelTestWritableClient, directory, &entry) == UMICOM_VFS_CHANGED);
        CommitEqual(&entry, &before, sizeof(entry));
        CHECK(umicomKernelTestWritableClient.descriptions[0].position == cursor);
        CHECK(umicomKernelTestWritableMount.provider.directoryEpoch == epoch + 1U);
        CHECK(UmicomKernelVfsRewindDirectory(&umicomKernelTestWritableClient, directory) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsReadDirectory(&umicomKernelTestWritableClient, directory, &entry) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "write-stable")) {
        const UmicomKernelFileDescriptor file = UmicomKernelWritableTestDescriptor("/FRAG.BIN", UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE);
        UmicomSize written = 0U;
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, file, "x", 1U, &written) == UMICOM_VFS_OK && written == 1U);
        CHECK(umicomKernelTestWritableMount.provider.directoryEpoch == epoch);
        CHECK(UmicomKernelVfsReadDirectory(&umicomKernelTestWritableClient, directory, &entry) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "refusal-stable")) {
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_EXISTS);
        CHECK(umicomKernelTestWritableMount.provider.directoryEpoch == epoch && !commitWrites);
        CHECK(UmicomKernelVfsReadDirectory(&umicomKernelTestWritableClient, directory, &entry) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "epoch-exhausted")) {
        umicomKernelTestWritableMount.provider.directoryEpoch = ~(UmicomU64)0U;
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_EXHAUSTED);
        CHECK(!commitWrites && !commitFlushes);
    } else if (!strcmp(name, "remove-open")) {
        const UmicomKernelFileDescriptor file = UmicomKernelWritableTestDescriptor("/EMPTY.TXT", UMICOM_VFS_RIGHT_READ, UMICOM_FALSE);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_BUSY);
        CHECK(!commitWrites && !commitFlushes);
        CHECK(UmicomKernelVfsClose(&umicomKernelTestWritableClient, file) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/EMPTY.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
    } else {
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/WORK", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsCreate(&umicomKernelTestWritableClient, "/WORK/NOTE.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/WORK", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_NOT_EMPTY);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/WORK/NOTE.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsRemove(&umicomKernelTestWritableClient, "/WORK", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_OK);
        UmicomKernelWritableTestAccepted(4U);
    }
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestFailure(const char *name)
{
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/FRAG.BIN",
        UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
    const UmicomKernelVfsNodeInfo info = UmicomKernelWritableTestInfo(descriptor);
    UmicomSize transferred = 99U;
    if (!strcmp(name, "read") || !strcmp(name, "timeout")) {
        if (!strcmp(name, "timeout")) model.noCompletion = UMICOM_TRUE;
        else commitFailRead = commitReads + 1U;
        memset(umicomKernelTestWritableOutput, 0xa5, sizeof(umicomKernelTestWritableOutput));
        memcpy(umicomKernelTestWritableBefore, umicomKernelTestWritableOutput, sizeof(umicomKernelTestWritableOutput));
        CHECK(UmicomKernelVfsRead(&umicomKernelTestWritableClient, descriptor,
            umicomKernelTestWritableOutput, 100U, &transferred) != UMICOM_VFS_OK);
        CommitEqual(umicomKernelTestWritableOutput, umicomKernelTestWritableBefore, sizeof(umicomKernelTestWritableOutput));
    } else {
        if (!strcmp(name, "write")) { commitFailWrite = 3U; commitPartialWrite = UMICOM_TRUE; }
        else if (!strcmp(name, "flush")) commitFailFlush = 3U;
        else if (!strcmp(name, "drop")) commitDropWrite = 3U;
        else { commitCutMutation = 5U; commitPersistWrites = !strcmp(name, "cut-eager") ? UMICOM_TRUE : UMICOM_FALSE; }
        CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor,
            commitInput, 700U, &transferred) != UMICOM_VFS_OK);
        CHECK(!umicomKernelTestWritableMount.lifecycle.lastResult.commit.commitAccepted);
    }
    CHECK(!transferred && !umicomKernelTestWritableMount.lifecycle.committedOperations);
    CHECK(!umicomKernelTestWritableClient.descriptions[0].position);
    CHECK(UmicomKernelWritableTestInfo(descriptor).bytes == info.bytes);
    CHECK(umicomKernelTestWritableMount.provider.mediaFailed);
    model.allowed = UMICOM_TRUE;
    const UmicomU32 events = commitEventCount;
    CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, "x", 1U, &transferred) != UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsWrite(&umicomKernelTestWritableClient, descriptor, NULL, 0U, &transferred) != UMICOM_VFS_OK);
    CHECK(events == commitEventCount);
    UmicomKernelWritableTestClose();
}
static void UmicomKernelWritableTestGuards(const char *name)
{
    UmicomKernelWritableTestOpen();
    const UmicomKernelFileDescriptor descriptor = UmicomKernelWritableTestDescriptor("/FRAG.BIN",
        UMICOM_KERNEL_WRITABLE_TEST_RIGHTS, UMICOM_FALSE);
    const UmicomKernelVfsNodeInfo info = UmicomKernelWritableTestInfo(descriptor);
    UmicomKernelFat16WritableProvider *const provider = &umicomKernelTestWritableMount.provider;
    const UmicomKernelVfsOperations *const operations = UmicomKernelWritableTestOperations();
    UmicomSize transferred = 71U;
    const UmicomU32 events = commitEventCount;
    if (!strcmp(name, "provider-output"))
        CHECK(operations->read(provider, info.id, 0U, provider->zeroStage, 1U, &transferred) == UMICOM_VFS_INVALID_ARGUMENT);
    else if (!strcmp(name, "domain-output"))
        CHECK(operations->read(provider, info.id, 0U, &domain, 1U, &transferred) == UMICOM_VFS_INVALID_ARGUMENT);
    else if (!strcmp(name, "dma-output"))
        CHECK(operations->read(provider, info.id, 0U, (void *)domain.slots[0].dataFrame, 1U, &transferred) == UMICOM_VFS_INVALID_ARGUMENT);
    else if (!strcmp(name, "overlap"))
        CHECK(operations->read(provider, info.id, 0U, &transferred, sizeof(transferred), &transferred) == UMICOM_VFS_INVALID_ARGUMENT);
    else if (!strcmp(name, "alignment")) {
        _Alignas(UmicomSize) UmicomU8 unaligned[sizeof(UmicomSize) + 1U];
        memset(unaligned, 0xa5, sizeof(unaligned));
        CHECK(operations->read(provider, info.id, 0U, umicomKernelTestWritableOutput, 1U,
            (UmicomSize *)(void *)(unaligned + 1U)) == UMICOM_VFS_INVALID_ARGUMENT);
        for (UmicomSize i = 0U; i < sizeof(unaligned); ++i) CHECK(unaligned[i] == 0xa5U);
    } else if (!strcmp(name, "busy")) {
        provider->busy = UMICOM_TRUE;
        CHECK(operations->write(provider, info.id, 0U, "x", 1U, &transferred) == UMICOM_VFS_BUSY);
        CHECK(UmicomKernelDiskWritableMountClose(&umicomKernelTestWritableMount) == UMICOM_VFS_BUSY);
        provider->busy = UMICOM_FALSE;
    } else if (!strcmp(name, "drift")) {
        ++umicomKernelTestWritableMount.lifecycle.committedOperations;
        CHECK(operations->write(provider, info.id, 0U, "x", 1U, &transferred) != UMICOM_VFS_OK);
        CHECK(provider->mediaFailed);
    } else if (!strcmp(name, "updater-state") || !strcmp(name, "slot-state")) {
        /* An outer READY lifecycle is insufficient if its retained transport
         * is already closing. Cached lookup and no-ops must notice the lease,
         * while local metadata and descriptor release remain available. */
        if (!strcmp(name, "updater-state"))
            umicomKernelTestWritableMount.lifecycle.commit.updater.state = UMICOM_FAT16_UPDATER_CLOSING;
        else domain.slots[0].state = UMICOM_BLOCK_FAULTED;
        UmicomKernelVfsNodeId untouched = 97U;
        CHECK(operations->lookup(provider, 1U, "FRAG.BIN", &untouched) != UMICOM_VFS_OK && untouched == 97U);
        CHECK(provider->mediaFailed);
        CHECK(operations->read(provider, info.id, 0U, NULL, 0U, &transferred) != UMICOM_VFS_OK);
        CHECK(operations->write(provider, info.id, 0U, NULL, 0U, &transferred) != UMICOM_VFS_OK);
        CHECK(operations->resize(provider, info.id, info.bytes) != UMICOM_VFS_OK);
        CHECK(UmicomKernelWritableTestInfo(descriptor).bytes == info.bytes);
    } else {
        const UmicomKernelFat16FileTime before = provider->time;
        UmicomKernelFat16FileTime invalid = {2100U, 2U, 29U, 0U, 0U, 0U};
        CHECK(UmicomKernelDiskWritableMountSetTime(&umicomKernelTestWritableMount, &invalid) == UMICOM_VFS_INVALID_ARGUMENT);
        CommitEqual(&provider->time, &before, sizeof(before));
        invalid = (UmicomKernelFat16FileTime){2044U, 3U, 1U, 0U, 0U, 1U};
        CHECK(UmicomKernelDiskWritableMountSetTime(&umicomKernelTestWritableMount, &invalid) == UMICOM_VFS_OK);
        CHECK(provider->time.month == 3U);
    }
    CHECK(events == commitEventCount);
    UmicomKernelWritableTestClose();
}
static UmicomKernelShellStatus UmicomKernelWritableTestCommand(const char *text)
{
    UmicomKernelShellCommand command;
    const UmicomKernelShellStatus parsed = UmicomKernelShellParse(text, (UmicomSize)strlen(text), &command);
    CHECK(parsed == UMICOM_SHELL_OK);
    UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus status = UmicomKernelDiskWritableFilesystemCommand(
        &umicomKernelTestWritableShell, &command, &handled);
    CHECK(handled);
    return status;
}
static void UmicomKernelWritableTestOutput(void *context, const char *text, UmicomSize bytes)
{
    Output(context, text, bytes);
    if (umicomKernelTestWritableReentry) {
        const UmicomSize before = transcriptBytes;
        CHECK(UmicomKernelWritableTestCommand("diskrwinfo") == UMICOM_SHELL_BUSY);
        CHECK(UmicomKernelDiskWritableFilesystemConsoleClose(&umicomKernelTestWritableShell) == UMICOM_VFS_BUSY);
        if (umicomKernelTestWritableArgumentReentry) {
            /* Preserve the original malformed-arity result while proving that
             * the new diagnostics cannot recurse through their own output. */
            CHECK(UmicomKernelWritableTestCommand("mountdiskrw") == UMICOM_SHELL_INVALID_ARGUMENT);
            CHECK(UmicomKernelWritableTestCommand("mountdiskrw 7 @ 2044-02-29T23:58:56") == UMICOM_SHELL_BUSY);
        }
        CHECK(transcriptBytes == before);
        ++umicomKernelTestWritableReentries;
    }
}
static void UmicomKernelWritableTestArgumentDiagnostic(const char *text,
    const char *field, const char *usage, const char *explanation)
{
    /* Check the whole transport model and lease domain, not just write counts:
     * a malformed command must not probe, reset, allocate or consume admission. */
    const BlockModel modelBefore = model;
    const UmicomKernelBlockDomain domainBefore = domain;
    const UmicomU32 events = commitEventCount;
    const UmicomSize allocated = Allocated();
    CommitClearTranscript();
    CHECK(UmicomKernelWritableTestCommand(text) == UMICOM_SHELL_INVALID_ARGUMENT);
    CHECK(strstr(transcript, "=invalid-argument field="));
    CHECK(strstr(transcript, field) && strstr(transcript, usage) && strstr(transcript, explanation));
    CHECK(!strstr(transcript, "diskrw.media=") && !strstr(transcript, "diskrw.transport="));
    CommitEqual(&model, &modelBefore, sizeof(model));
    CommitEqual(&domain, &domainBefore, sizeof(domain));
    CHECK(commitEventCount == events && Allocated() == allocated);
}
static void UmicomKernelWritableTestUnusedConsole(void)
{
    CommitClearTranscript();
    CHECK(UmicomKernelWritableTestCommand("diskrwinfo") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "diskrw.state=unused clients=0 last-commit-accepted=0\r\n"));
    CHECK(strstr(transcript, "diskrwinfo=ok committed-operations=0\r\n"));
    CHECK(!commitEventCount && !Allocated());
}
static void UmicomKernelWritableTestConsoleNumeric(void)
{
    const char *const numbers[] = {"-1", "+1", "0x0", "@", "1x", "18446744073709551616", "\"\""};
    char command[128];
    for (UmicomSize i = 0U; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        CHECK(snprintf(command, sizeof(command), "mountdiskrw %s 0 2044-02-29T23:58:56", numbers[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=slot", "usage: mountdiskrw SLOT PARTITION",
            "SLOT must be a decimal number from 0 to 7.");
        CHECK(snprintf(command, sizeof(command), "mountdiskrw 0 %s 2044-02-29T23:58:56", numbers[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=partition", "usage: mountdiskrw SLOT PARTITION",
            "PARTITION must be decimal 0..3; use 0 for the first partition.");
        CHECK(snprintf(command, sizeof(command), "diskwrite /A.TXT %s x", numbers[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=offset", "usage: diskwrite PATH OFFSET \"TEXT\"",
            "OFFSET must be a non-negative decimal byte offset");
        CHECK(snprintf(command, sizeof(command), "diskresize /A.TXT %s", numbers[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=size", "usage: diskresize PATH SIZE",
            "SIZE must be a non-negative decimal byte count");
    }
    UmicomKernelWritableTestArgumentDiagnostic("mountdiskrw 8 0 2044-02-29T23:58:56",
        "field=slot", "usage: mountdiskrw SLOT PARTITION", "0 to 7");
    UmicomKernelWritableTestArgumentDiagnostic("mountdiskrw 0 4 2044-02-29T23:58:56",
        "field=partition", "usage: mountdiskrw SLOT PARTITION", "0..3");
}
static void UmicomKernelWritableTestConsoleCalendar(void)
{
    const char *const calendars[] = {"2044-02-29", "2044-02-29t23:58:56", "2044/02/29T23:58:56",
        "2044-02-29T23:58:56Z", "2044-02-29T23:58:5X", "1979-12-31T00:00:00", "2108-01-01T00:00:00",
        "2100-02-29T00:00:00", "2044-00-01T00:00:00", "2044-13-01T00:00:00", "2044-02-00T00:00:00",
        "2044-02-30T00:00:00", "2044-04-31T00:00:00", "2044-02-29T24:00:00", "2044-02-29T00:60:00",
        "2044-02-29T00:00:60", "\"\""};
    char command[128];
    for (UmicomSize i = 0U; i < sizeof(calendars) / sizeof(calendars[0]); ++i) {
        CHECK(snprintf(command, sizeof(command), "mountdiskrw 0 0 %s", calendars[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=calendar",
            "usage: mountdiskrw SLOT PARTITION YYYY-MM-DDTHH:MM:SS", "year 1980..2107");
        CHECK(snprintf(command, sizeof(command), "diskrwtime %s", calendars[i]) > 0);
        UmicomKernelWritableTestArgumentDiagnostic(command, "field=calendar",
            "usage: diskrwtime YYYY-MM-DDTHH:MM:SS", "time 00:00:00..23:59:59");
    }
}
static UmicomBoolean UmicomKernelWritableTestConsoleDiagnostics(const char *name)
{
    if (!strcmp(name, "partition-typo") || !strcmp(name, "argument-reentry")) {
        umicomKernelTestWritableReentry = !strcmp(name, "argument-reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        umicomKernelTestWritableArgumentReentry = umicomKernelTestWritableReentry;
        /* The user supplied slot 7. Our model's real device is slot 0; the
         * invalid partition must fail before either slot is consulted. */
        UmicomKernelWritableTestArgumentDiagnostic("mountdiskrw 7 @ 2044-02-29T23:58:56",
            "mountdiskrw=invalid-argument field=partition\r\n",
            "usage: mountdiskrw SLOT PARTITION YYYY-MM-DDTHH:MM:SS\r\n",
            "PARTITION must be decimal 0..3; use 0 for the first partition.\r\n");
    } else if (!strcmp(name, "arity")) {
        static const struct { const char *missing; const char *extra; const char *usage; } cases[] = {
            {"mountdiskrw 0 0", NULL, "usage: mountdiskrw SLOT PARTITION YYYY-MM-DDTHH:MM:SS"},
            {NULL, "unmountdiskrw extra", "usage: unmountdiskrw\r\n"},
            {NULL, "diskrwinfo extra", "usage: diskrwinfo\r\n"},
            {"diskrwtime", "diskrwtime 2044-02-29T23:58:56 extra", "usage: diskrwtime YYYY-MM-DDTHH:MM:SS"},
            {"diskrwls", "diskrwls / extra", "usage: diskrwls PATH"},
            {"diskrwcat", "diskrwcat /A.TXT extra", "usage: diskrwcat PATH"},
            {"diskcreate", "diskcreate /A.TXT extra", "usage: diskcreate PATH"},
            {"diskmkdir", "diskmkdir /WORK extra", "usage: diskmkdir PATH"},
            {"diskdelete", "diskdelete /A.TXT extra", "usage: diskdelete PATH"},
            {"diskrmdir", "diskrmdir /WORK extra", "usage: diskrmdir PATH"},
            {"diskwrite /A.TXT 0", NULL, "usage: diskwrite PATH OFFSET \"TEXT\""},
            {"diskappend /A.TXT", "diskappend /A.TXT unquoted words", "usage: diskappend PATH \"TEXT\""},
            {"diskresize /A.TXT", "diskresize /A.TXT 0 extra", "usage: diskresize PATH SIZE"}
        };
        for (UmicomSize i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            if (cases[i].missing) UmicomKernelWritableTestArgumentDiagnostic(cases[i].missing,
                "field=arguments", cases[i].usage, "Incorrect number of arguments.");
            if (cases[i].extra) UmicomKernelWritableTestArgumentDiagnostic(cases[i].extra,
                "field=arguments", cases[i].usage, "Incorrect number of arguments.");
        }
        /* Five tokens exceed the shared parser's four-token command storage.
         * The outer console already reports a syntax error before dispatch;
         * retain that bound rather than letting this adapter bypass it. */
        const char *const overflow[] = {"mountdiskrw 0 0 2044-02-29T23:58:56 extra",
            "diskwrite /A.TXT 0 unquoted words"};
        for (UmicomSize i = 0U; i < sizeof(overflow) / sizeof(overflow[0]); ++i) {
            UmicomKernelShellCommand command, before;
            memset(&command, 0xa5, sizeof(command)); before = command;
            CHECK(UmicomKernelShellParse(overflow[i], (UmicomSize)strlen(overflow[i]), &command) == UMICOM_SHELL_SYNTAX);
            CommitEqual(&command, &before, sizeof(command));
        }
    } else if (!strcmp(name, "numeric")) UmicomKernelWritableTestConsoleNumeric();
    else if (!strcmp(name, "calendar")) UmicomKernelWritableTestConsoleCalendar();
    else if (!strcmp(name, "unmounted")) {
        const char *const commands[] = {"diskrwtime 2044-02-29T23:58:56", "diskrwls /", "diskrwcat /A.TXT",
            "diskcreate /A.TXT", "diskmkdir /WORK", "diskdelete /A.TXT", "diskrmdir /WORK",
            "diskwrite /A.TXT 0 x", "diskappend /A.TXT x", "diskresize /A.TXT 0"};
        const BlockModel modelBefore = model;
        for (UmicomSize i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {
            CommitClearTranscript();
            CHECK(UmicomKernelWritableTestCommand(commands[i]) == UMICOM_SHELL_IO_ERROR);
            CHECK(strstr(transcript, "=bad-state committed-operations=0\r\n"));
            CHECK(strstr(transcript, "No writable disk is mounted. Run disks, then mountdiskrw"));
            CHECK(strstr(transcript, "PARTITION is decimal 0..3; use 0 for the first partition."));
            CHECK(!strstr(transcript, "diskrw.media=") && !strstr(transcript, "media failure"));
            CommitEqual(&model, &modelBefore, sizeof(model));
            CHECK(!commitEventCount && !Allocated());
        }
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
    } else if (!strcmp(name, "mounted-arguments")) {
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_OK);
        UmicomKernelWritableTestConsoleNumeric();
        UmicomKernelWritableTestConsoleCalendar();
        CHECK(UmicomKernelWritableTestCommand("diskwrite /FRAG.BIN 0 x") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
        CHECK(!Allocated()); return UMICOM_TRUE;
    } else if (!strcmp(name, "owner-diagnostics")) {
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_OK);
        UmicomKernelConsoleShell other = {0}; other.output = UmicomKernelWritableTestOutput;
        const char *const requests[] = {"mountdiskrw", "mountdiskrw 7 @ 2044-02-29T23:58:56", "diskrwinfo"};
        const BlockModel modelBefore = model;
        for (UmicomSize i = 0U; i < sizeof(requests) / sizeof(requests[0]); ++i) {
            UmicomKernelShellCommand command; UmicomBoolean handled = UMICOM_FALSE;
            CHECK(UmicomKernelShellParse(requests[i], (UmicomSize)strlen(requests[i]), &command) == UMICOM_SHELL_OK);
            CommitClearTranscript();
            CHECK(UmicomKernelDiskWritableFilesystemCommand(&other, &command, &handled) ==
                (i ? UMICOM_SHELL_BAD_STATE : UMICOM_SHELL_INVALID_ARGUMENT));
            CHECK(handled && !transcriptBytes);
        }
        CHECK(UmicomKernelDiskWritableFilesystemConsoleClose(&other) == UMICOM_VFS_ACCESS_DENIED);
        CommitEqual(&model, &modelBefore, sizeof(model));
        CHECK(UmicomKernelWritableTestCommand("diskrwinfo") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
        CHECK(!Allocated()); return UMICOM_TRUE;
    } else if (!strcmp(name, "lifetime-hints")) {
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_OK);
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "A writable disk is already mounted."));
        model.stuckReset = UMICOM_TRUE; model.step = 1000000U;
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "Writable disk cleanup is incomplete. Retry unmountdiskrw"));
        CHECK(Allocated());
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("diskmkdir /WORK") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "Writable disk cleanup is incomplete. Retry unmountdiskrw"));
        CHECK(!strstr(transcript, "No writable disk is mounted."));
        model.stuckReset = UMICOM_FALSE; model.step = 1U;
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("diskmkdir /WORK") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "This writable mount lifetime is closed. Restart for a new mount."));
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_IO_ERROR);
        CHECK(!Allocated() && !commitWrites && !commitFlushes); return UMICOM_TRUE;
    } else if (!strcmp(name, "failed-admission-hint")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "mountdiskrw=read-only") && strstr(transcript, "diskrw.media="));
        CHECK(strstr(transcript, "This writable mount lifetime is closed. Restart for a new mount."));
        CHECK(!strstr(transcript, "No writable disk is mounted."));
        model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
        CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_IO_ERROR);
        CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
        CHECK(!Allocated() && !commitWrites && !commitFlushes); return UMICOM_TRUE;
    } else return UMICOM_FALSE;
    UmicomKernelWritableTestUnusedConsole();
    CommitUnchanged();
    /* Invalid input and pre-mount queries leave the one admitted attempt
     * available. A corrected command then performs real filesystem work. */
    CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:56") == UMICOM_SHELL_OK);
    CHECK(UmicomKernelWritableTestCommand("diskmkdir /WORK") == UMICOM_SHELL_OK);
    CHECK(UmicomKernelWritableTestCommand("diskcreate /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
    CHECK(UmicomKernelWritableTestCommand("diskwrite /WORK/NOTE.TXT 0 hello") == UMICOM_SHELL_OK);
    CHECK(UmicomKernelWritableTestCommand("diskrwcat /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "hello\r\n") && strstr(transcript, "diskwrite=ok committed-operations=3"));
    CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
    if (umicomKernelTestWritableReentry) CHECK(umicomKernelTestWritableReentries);
    CHECK(!Allocated()); return UMICOM_TRUE;
}
static void UmicomKernelWritableTestConsole(const char *name)
{
    umicomKernelTestWritableShell.output = UmicomKernelWritableTestOutput;
    if (UmicomKernelWritableTestConsoleDiagnostics(name)) return;
    if (!strcmp(name, "arguments")) {
        const char *const invalid[] = {"mountdiskrw", "mountdiskrw 0 0 2100-02-29T00:00:00",
            "mountdiskrw -1 0 2044-02-29T23:58:57", "diskwrite /A.TXT -1 x", "diskresize /A.TXT -1",
            "diskcreate", "diskrwtime 2044-02-30T00:00:00", "unmountdiskrw extra"};
        for (UmicomSize i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            CHECK(UmicomKernelWritableTestCommand(invalid[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!commitEventCount && !Allocated()); return;
    }
    umicomKernelTestWritableReentry = !strcmp(name, "reentry") ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(UmicomKernelWritableTestCommand("mountdiskrw 0 0 2044-02-29T23:58:57") == UMICOM_SHELL_OK);
    if (!strcmp(name, "failure-close")) {
        commitFailWrite = 3U;
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("diskwrite /FRAG.BIN 0 x") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "diskrw.media=") && strstr(transcript, "block=") && strstr(transcript, "cleanup="));
        CHECK(strstr(transcript, "Writable disk I/O is unavailable after a media failure."));
        CHECK(!strstr(transcript, "No writable disk is mounted.") && !strstr(transcript, "usage:"));
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("diskwrite /FRAG.BIN 0 x") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "Writable disk I/O is unavailable after a media failure."));
        CHECK(!strstr(transcript, "No writable disk is mounted."));
    } else if (!strcmp(name, "binary")) {
        CommitClearTranscript();
        CHECK(UmicomKernelWritableTestCommand("diskrwcat /FRAG.BIN") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "\\x") != NULL);
    } else {
        CHECK(UmicomKernelWritableTestCommand("diskmkdir /WORK") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskcreate /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskwrite /WORK/NOTE.TXT 0 hello") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskappend /WORK/NOTE.TXT world") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskresize /WORK/NOTE.TXT 12") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskrwcat /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskrwls /WORK") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskrwinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "helloworld\\x00\\x00") && strstr(transcript, "last-commit-accepted=1"));
        CHECK(UmicomKernelWritableTestCommand("diskdelete /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelWritableTestCommand("diskrmdir /WORK") == UMICOM_SHELL_OK);
    }
    const UmicomU32 mutations = commitMutations;
    CHECK(UmicomKernelWritableTestCommand("unmountdiskrw") == UMICOM_SHELL_OK);
    CHECK(!Allocated() && mutations == commitMutations);
    if (umicomKernelTestWritableReentry) CHECK(umicomKernelTestWritableReentries);
}
int __wrap_main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: umicom-disk-writable-filesystem-tests CASE FIXTURE\n"); return 2; }
    CommitStart(argv[2]);
    const char *const name = argv[1];
    if (!strncmp(name, "mount.", 6U)) UmicomKernelWritableTestMount(name + 6U);
    else if (!strncmp(name, "write.", 6U)) UmicomKernelWritableTestWrite(name + 6U);
    else if (!strncmp(name, "resize.", 7U)) UmicomKernelWritableTestResize(name + 7U);
    else if (!strncmp(name, "rights.", 7U)) UmicomKernelWritableTestRights(name + 7U);
    else if (!strncmp(name, "identity.", 9U)) UmicomKernelWritableTestIdentity(name + 9U);
    else if (!strncmp(name, "directory.", 10U)) UmicomKernelWritableTestDirectory(name + 10U);
    else if (!strncmp(name, "failure.", 8U)) UmicomKernelWritableTestFailure(name + 8U);
    else if (!strncmp(name, "guards.", 7U)) UmicomKernelWritableTestGuards(name + 7U);
    else if (!strncmp(name, "console.", 8U)) UmicomKernelWritableTestConsole(name + 8U);
    else CHECK(0);
    CHECK(!Allocated()); printf("disk-writable-filesystem.%s: ok\n", name); return 0;
}
