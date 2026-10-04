/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/message_channels/message_tests.c
 *
 * PURPOSE:
 *   Exercise the real message domain and user-call adapter with actual allocator
 *   and page-table code. Native execution tests policy, not RISC-V trap entry.
 *
 * EDUCATIONAL NOTE:
 *   Each CTest starts a fresh process. Ordinary teardown uses public close and
 *   destroy calls, never a memset which would hide a leak. Direct field edits
 *   below are labelled corruption/exhaustion injection, not client examples.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/message_service.h"
#include "umicom/kernel/physical_memory.h"

static UmicomKernelMessageDomain umicomTestDomain;
static UmicomKernelMessageDomain umicomTestOther;
static UmicomKernelMessageHandle umicomTestLeft;
static UmicomKernelMessageHandle umicomTestRight;
static unsigned umicomTestChecks;
alignas(4096) static UmicomU8 umicomTestRam[4096U * 128U];
static UmicomKernelVirtualAddressSpace umicomTestSpace;
static UmicomKernelUserPage umicomTestPages[3];
static UmicomAddress umicomTestBacking[4];
static UmicomKernelUserSession umicomTestSession;
static UmicomBoolean umicomTestMapped;
static UmicomBoolean umicomTestBound;
#define UMICOM_TEST_CODE ((UmicomAddress)0x400000U)
#define UMICOM_TEST_DATA ((UmicomAddress)0x500000U)

static void UmicomTestRequire(int condition, const char *reason)
{
    ++umicomTestChecks;
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", reason);
        exit(EXIT_FAILURE);
    }
}
static void UmicomTestExpect(UmicomKernelMessageStatus actual, UmicomKernelMessageStatus expected, const char *reason)
{
    if (actual != expected) fprintf(stderr, "%s: expected %s, got %s\n", reason,
        UmicomKernelMessageStatusName(expected), UmicomKernelMessageStatusName(actual));
    UmicomTestRequire(actual == expected, reason);
}
static void UmicomTestInitialize(void)
{
    UmicomTestExpect(UmicomKernelMessageInitialize(&umicomTestDomain), UMICOM_MESSAGE_OK, "initialise once");
}
static void UmicomTestPair(UmicomKernelMessageRights left, UmicomKernelMessageRights right)
{
    UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestDomain, 11U, left, 22U, right,
        &umicomTestLeft, &umicomTestRight), UMICOM_MESSAGE_OK, "create pair");
}
static void UmicomTestSend(UmicomU8 value)
{
    UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, &value, 1U),
        UMICOM_MESSAGE_OK, "send one byte");
}
static UmicomKernelMessage UmicomTestReceive(void)
{
    UmicomKernelMessage message;
    UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, umicomTestRight,
        &message, UMICOM_MESSAGE_MAX_BYTES), UMICOM_MESSAGE_OK, "receive whole packet");
    return message;
}
static UmicomKernelMessageSnapshot UmicomTestSnapshot(void)
{
    UmicomKernelMessageSnapshot snapshot;
    UmicomTestExpect(UmicomKernelMessageSnapshotRead(&umicomTestDomain, &snapshot), UMICOM_MESSAGE_OK, "recount invariants");
    return snapshot;
}
static void UmicomTestTeardownDomain(UmicomKernelMessageDomain *domain)
{
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        if (domain->handles[index].occupied == UMICOM_TRUE) {
            UmicomSize count = 0U;
            UmicomTestExpect(UmicomKernelMessageCloseOwner(domain, domain->handles[index].owner, &count),
                UMICOM_MESSAGE_OK, "close remaining owner");
            UmicomTestRequire(count != 0U, "cleanup made progress");
        }
    }
    UmicomKernelMessageSnapshot snapshot;
    UmicomTestExpect(UmicomKernelMessageSnapshotRead(domain, &snapshot), UMICOM_MESSAGE_OK, "final domain invariants");
    UmicomTestRequire(snapshot.channels == 0U && snapshot.handles == 0U && snapshot.messages == 0U,
        "no channel, reference or packet leaked");
}
static void UmicomTestMapPrepare(void)
{
    /* Non-contiguous backing pages prove that the adapter does not turn a
     * user virtual span into one unchecked machine-mode pointer. */
    UmicomTestRequire(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomTestRam, sizeof(umicomTestRam)) == UMICOM_KERNEL_MEMORY_OK, "physical allocator");
    UmicomTestRequire(UmicomKernelVirtualAddressSpaceCreate(&umicomTestSpace) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "test page root");
    for (UmicomSize index = 0U; index < 4U; ++index) {
        UmicomTestRequire(UmicomKernelPhysicalMemoryAllocateFrame(&umicomTestBacking[index]) == UMICOM_KERNEL_MEMORY_OK, "allocate backing");
        memset((void *)umicomTestBacking[index], 0xcc, 4096U);
    }
    umicomTestPages[0] = (UmicomKernelUserPage){ UMICOM_TEST_CODE, umicomTestBacking[0],
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE | UMICOM_KERNEL_VIRTUAL_MEMORY_USER };
    umicomTestPages[1] = (UmicomKernelUserPage){ UMICOM_TEST_DATA, umicomTestBacking[1],
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE | UMICOM_KERNEL_VIRTUAL_MEMORY_USER };
    umicomTestPages[2] = (UmicomKernelUserPage){ UMICOM_TEST_DATA + 4096U, umicomTestBacking[3],
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE | UMICOM_KERNEL_VIRTUAL_MEMORY_USER };
    for (UmicomSize index = 0U; index < 3U; ++index) {
        UmicomTestRequire(UmicomKernelVirtualMemoryMapPage(&umicomTestSpace, umicomTestPages[index].virtualBase,
            umicomTestPages[index].physicalBase, umicomTestPages[index].permissions) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "map actual leaf");
    }
    umicomTestSession.memory.space = &umicomTestSpace;
    umicomTestSession.memory.pages = umicomTestPages;
    umicomTestSession.memory.pageCount = 3U;
    umicomTestSession.identity = 11U;
    umicomTestMapped = UMICOM_TRUE;
}
static void UmicomTestBind(UmicomU64 identity)
{
    umicomTestSession.identity = identity;
    UmicomTestExpect(UmicomKernelMessageServiceBind(&umicomTestDomain, identity), UMICOM_MESSAGE_OK, "bind trusted session");
    umicomTestBound = UMICOM_TRUE;
}
static UmicomRiscvTrapFrame UmicomTestFrame(UmicomU64 call, UmicomU64 token, UmicomAddress buffer, UmicomSize bytes)
{
    UmicomRiscvTrapFrame frame = { 0 };
    frame.mcause = 8U;
    frame.mepc = UMICOM_TEST_CODE;
    frame.x17_a7 = call;
    frame.x10_a0 = token;
    frame.x11_a1 = buffer;
    frame.x12_a2 = bytes;
    frame.x1_ra = 0xabcdefU;
    frame.x2_sp = 0x123456U; /* Not dereferenced: registers carry the call. */
    return frame;
}
static UmicomKernelMessageStatus UmicomTestCall(UmicomU64 call, UmicomU64 token, UmicomAddress buffer, UmicomSize bytes)
{
    UmicomRiscvTrapFrame frame = UmicomTestFrame(call, token, buffer, bytes);
    UmicomTestRequire(UmicomKernelUserTrapDispatch(&umicomTestSession, &frame) == 1U, "syscall resumes user context");
    UmicomTestRequire(frame.mepc == UMICOM_TEST_CODE + 4U && frame.x1_ra == 0xabcdefU && frame.x2_sp == 0x123456U,
        "PC advances and saved context remains");
    return (UmicomKernelMessageStatus)frame.x10_a0;
}
static void UmicomTestTeardown(void)
{
    if (umicomTestBound != UMICOM_FALSE) {
        UmicomTestExpect(UmicomKernelMessageServiceUnbind(&umicomTestDomain, umicomTestSession.identity), UMICOM_MESSAGE_OK, "unbind final session");
    }
    UmicomTestTeardownDomain(&umicomTestDomain);
    if (umicomTestMapped != UMICOM_FALSE) {
        UmicomTestRequire(UmicomKernelVirtualAddressSpaceDestroy(&umicomTestSpace) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "destroy test hierarchy");
        for (UmicomSize index = 0U; index < 4U; ++index) {
            UmicomTestRequire(UmicomKernelPhysicalMemoryFreeFrame(umicomTestBacking[index]) == UMICOM_KERNEL_MEMORY_OK, "free backing frame");
        }
        UmicomKernelPhysicalMemorySnapshot snapshot;
        UmicomTestRequire(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK && snapshot.allocatedFrames == 0U &&
            UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "native fixture has no frame leak");
    }
}
static int UmicomTestIs(const char *actual, const char *expected) { return strcmp(actual, expected) == 0; }
int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    const char *const name = argv[1];
    if (UmicomTestIs(name, "null-domain")) {
        UmicomTestExpect(UmicomKernelMessageInitialize(NULL), UMICOM_MESSAGE_INVALID_ARGUMENT, name);
        return EXIT_SUCCESS;
    }
    if (UmicomTestIs(name, "uninitialised")) {
        UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_NOT_INITIALISED, name);
        return EXIT_SUCCESS;
    }
    if (UmicomTestIs(name, "dirty-storage")) {
        ((UmicomU8 *)&umicomTestDomain)[sizeof(umicomTestDomain) - 1U] = 1U;
        UmicomTestExpect(UmicomKernelMessageInitialize(&umicomTestDomain), UMICOM_MESSAGE_BAD_STATE, name);
        return EXIT_SUCCESS;
    }
    UmicomTestInitialize();
    UmicomKernelMessageHandle output = 0xaaaaU;
    UmicomKernelMessageInfo info;
    UmicomKernelMessage message;
    UmicomU8 payload[UMICOM_MESSAGE_MAX_BYTES];
    memset(payload, 0x5a, sizeof(payload));
    if (UmicomTestIs(name, "reinitialise")) {
        UmicomTestExpect(UmicomKernelMessageInitialize(&umicomTestDomain), UMICOM_MESSAGE_BAD_STATE, name);
    } else if (UmicomTestIs(name, "copied-domain")) {
        memcpy(&umicomTestOther, &umicomTestDomain, sizeof(umicomTestOther));
        UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestOther), UMICOM_MESSAGE_BAD_STATE, name);
    } else if (UmicomTestIs(name, "invalid-owner") || UmicomTestIs(name, "unknown-rights") ||
        UmicomTestIs(name, "aliased-output") || UmicomTestIs(name, "internal-output")) {
        UmicomKernelMessageHandle second = 0xbbbbU;
        UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestDomain,
            UmicomTestIs(name, "invalid-owner") ? 0U : 11U,
            UmicomTestIs(name, "unknown-rights") ? 0x8000U : UMICOM_MESSAGE_RIGHT_ALL,
            22U, UMICOM_MESSAGE_RIGHT_ALL,
            UmicomTestIs(name, "internal-output") ? &umicomTestDomain.handles[0].owner : &output,
            UmicomTestIs(name, "aliased-output") ? &output : &second), UMICOM_MESSAGE_INVALID_ARGUMENT, name);
        UmicomTestRequire(output == 0xaaaaU && second == 0xbbbbU && UmicomTestSnapshot().handles == 0U, "atomic refusal");
    } else if (UmicomTestIs(name, "channel-capacity")) {
        for (UmicomU64 index = 0U; index < UMICOM_MESSAGE_MAX_CHANNELS; ++index) {
            UmicomKernelMessageHandle a, b;
            UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestDomain, index * 2U + 1U, 0U, index * 2U + 2U, 0U, &a, &b), UMICOM_MESSAGE_OK, "fill channels");
        }
        UmicomKernelMessageHandle second;
        UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestDomain, 100U, 0U, 101U, 0U, &output, &second), UMICOM_MESSAGE_CHANNEL_LIMIT, name);
        UmicomTestRequire(output == 0xaaaaU, "failed admission leaves output alone");
    } else {
        UmicomTestPair(UMICOM_MESSAGE_RIGHT_ALL, UMICOM_MESSAGE_RIGHT_ALL);
        if (UmicomTestIs(name, "send-receive") || UmicomTestIs(name, "source-copy")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 32U), UMICOM_MESSAGE_OK, "queue bytes");
            payload[0] = 0xffU;
            message = UmicomTestReceive();
            UmicomTestRequire(message.sender == 11U && message.sequence == 1U && message.bytes == 32U && message.data[0] == 0x5aU, name);
        } else if (UmicomTestIs(name, "duplex")) {
            UmicomTestSend(1U);
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 22U, umicomTestRight, payload, 3U), UMICOM_MESSAGE_OK, "send reverse");
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 11U, umicomTestLeft, &message, 256U), UMICOM_MESSAGE_OK, name);
            UmicomTestRequire(message.sender == 22U && message.sequence == 2U && UmicomTestReceive().data[0] == 1U, "duplex sequence");
        } else if (UmicomTestIs(name, "empty-queue")) {
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, umicomTestRight, &message, 256U), UMICOM_MESSAGE_WOULD_BLOCK, name);
        } else if (UmicomTestIs(name, "full-queue")) {
            for (UmicomU8 index = 0U; index < 4U; ++index) UmicomTestSend(index);
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 1U), UMICOM_MESSAGE_WOULD_BLOCK, name);
            for (UmicomU8 index = 0U; index < 4U; ++index) UmicomTestRequire(UmicomTestReceive().data[0] == index, "no eviction; FIFO order");
        } else if (UmicomTestIs(name, "zero-payload")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 0U), UMICOM_MESSAGE_INVALID_ARGUMENT, name);
        } else if (UmicomTestIs(name, "oversized-payload")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 257U), UMICOM_MESSAGE_TOO_LARGE, name);
        } else if (UmicomTestIs(name, "maximum-payload")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 256U), UMICOM_MESSAGE_OK, name);
            message = UmicomTestReceive();
            UmicomTestRequire(message.bytes == 256U && memcmp(message.data, payload, 256U) == 0, "full payload copied");
        } else if (UmicomTestIs(name, "internal-source")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft,
                (const UmicomU8 *)&umicomTestDomain, 1U), UMICOM_MESSAGE_INVALID_ARGUMENT, name);
        } else if (UmicomTestIs(name, "short-destination")) {
            UmicomTestSend(44U); memset(&message, 0xaa, sizeof(message));
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, umicomTestRight, &message, 0U), UMICOM_MESSAGE_BUFFER_TOO_SMALL, name);
            UmicomTestRequire(message.data[0] == 0xaaU && UmicomTestSnapshot().messages == 1U, "no truncation, write or pop");
        } else if (UmicomTestIs(name, "peek-preserves") || UmicomTestIs(name, "consume-sequence")) {
            UmicomTestSend(44U);
            UmicomTestExpect(UmicomKernelMessagePeek(&umicomTestDomain, 22U, umicomTestRight, &message), UMICOM_MESSAGE_OK, "peek");
            UmicomTestExpect(UmicomKernelMessageConsume(&umicomTestDomain, 22U, umicomTestRight, message.sequence + 1U), UMICOM_MESSAGE_STATE_CHANGED, "refuse stale front");
            UmicomTestRequire(UmicomTestSnapshot().messages == 1U && UmicomTestReceive().data[0] == 44U, name);
        } else if (UmicomTestIs(name, "peer-close-drain")) {
            UmicomTestSend(9U);
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 11U, umicomTestLeft), UMICOM_MESSAGE_OK, "close sender");
            UmicomTestExpect(UmicomKernelMessageQuery(&umicomTestDomain, 22U, umicomTestRight, &info), UMICOM_MESSAGE_OK, "query closed peer");
            UmicomTestRequire((info.flags & 5U) == 5U && UmicomTestReceive().data[0] == 9U, name);
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, umicomTestRight, &message, 256U), UMICOM_MESSAGE_PEER_CLOSED, "closed after drain");
        } else if (UmicomTestIs(name, "receiver-close-discard")) {
            UmicomTestSend(9U);
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 22U, umicomTestRight), UMICOM_MESSAGE_OK, "close receiver");
            UmicomTestRequire(UmicomTestSnapshot().messages == 0U, "dead receiver's queue removed");
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 1U), UMICOM_MESSAGE_PEER_CLOSED, name);
        } else if (UmicomTestIs(name, "reference-survives") || UmicomTestIs(name, "duplicate-subset")) {
            UmicomTestExpect(UmicomKernelMessageDuplicate(&umicomTestDomain, 22U, umicomTestRight,
                UMICOM_MESSAGE_RIGHT_RECEIVE, &output), UMICOM_MESSAGE_OK, "duplicate receiver");
            UmicomTestSend(7U);
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 22U, umicomTestRight), UMICOM_MESSAGE_OK, "one alias closed");
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 22U, output, payload, 1U), UMICOM_MESSAGE_ACCESS_DENIED, "duplicate cannot send");
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, output, &message, 256U), UMICOM_MESSAGE_OK, name);
        } else if (UmicomTestIs(name, "grant-owner") || UmicomTestIs(name, "close-owner-shared")) {
            UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 22U, umicomTestRight, 33U,
                UMICOM_MESSAGE_RIGHT_RECEIVE, &output), UMICOM_MESSAGE_OK, "grant to another principal");
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 22U, output, &message, 256U), UMICOM_MESSAGE_WRONG_OWNER, "copied handle not authority");
            UmicomSize closed = 0U;
            UmicomTestExpect(UmicomKernelMessageCloseOwner(&umicomTestDomain, 22U, &closed), UMICOM_MESSAGE_OK, "original owner cleanup");
            UmicomTestSend(8U);
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestDomain, 33U, output, &message, 256U), UMICOM_MESSAGE_OK, name);
        } else if (UmicomTestIs(name, "duplicate-right") || UmicomTestIs(name, "grant-right")) {
            UmicomTestExpect(UmicomKernelMessageRestrict(&umicomTestDomain, 11U, umicomTestLeft,
                UMICOM_MESSAGE_RIGHT_SEND | (UmicomTestIs(name, "grant-right") ? UMICOM_MESSAGE_RIGHT_DUPLICATE : 0U)), UMICOM_MESSAGE_OK, "reduce source rights");
            const UmicomKernelMessageStatus status = UmicomTestIs(name, "grant-right")
                ? UmicomKernelMessageGrant(&umicomTestDomain, 11U, umicomTestLeft, 33U, UMICOM_MESSAGE_RIGHT_SEND, &output)
                : UmicomKernelMessageDuplicate(&umicomTestDomain, 11U, umicomTestLeft, UMICOM_MESSAGE_RIGHT_SEND, &output);
            UmicomTestExpect(status, UMICOM_MESSAGE_ACCESS_DENIED, name);
            UmicomTestRequire(output == 0xaaaaU, "refused token not published");
        } else if (UmicomTestIs(name, "grant-no-escalation") || UmicomTestIs(name, "irreversible-restrict")) {
            UmicomTestExpect(UmicomKernelMessageRestrict(&umicomTestDomain, 11U, umicomTestLeft,
                UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_DUPLICATE | UMICOM_MESSAGE_RIGHT_TRANSFER), UMICOM_MESSAGE_OK, "remove receive");
            UmicomTestExpect(UmicomKernelMessageRestrict(&umicomTestDomain, 11U, umicomTestLeft, UMICOM_MESSAGE_RIGHT_ALL), UMICOM_MESSAGE_ACCESS_DENIED, "restriction irreversible");
            UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 11U, umicomTestLeft, 33U,
                UMICOM_MESSAGE_RIGHT_RECEIVE, &output), UMICOM_MESSAGE_ACCESS_DENIED, name);
        } else if (UmicomTestIs(name, "zero-rights-close")) {
            UmicomTestExpect(UmicomKernelMessageRestrict(&umicomTestDomain, 11U, umicomTestLeft, 0U), UMICOM_MESSAGE_OK, "give up authority");
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 11U, umicomTestLeft), UMICOM_MESSAGE_OK, name);
        } else if (UmicomTestIs(name, "stale-handle") || UmicomTestIs(name, "generation-reuse")) {
            const UmicomKernelMessageHandle stale = umicomTestLeft;
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 11U, umicomTestLeft), UMICOM_MESSAGE_OK, "close old reference");
            UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 22U, umicomTestRight, 11U,
                UMICOM_MESSAGE_RIGHT_QUERY, &output), UMICOM_MESSAGE_OK, "reuse slot without reviving token");
            UmicomTestRequire(output != stale, "generation changed");
            UmicomTestExpect(UmicomKernelMessageQuery(&umicomTestDomain, 11U, stale, &info), UMICOM_MESSAGE_INVALID_HANDLE, name);
        } else if (UmicomTestIs(name, "generation-retirement")) {
            /* Exhaustion injection: fast-forward one live slot's generation. */
            umicomTestDomain.handles[0].generation = ~(UmicomU32)0U;
            const UmicomKernelMessageHandle final = ((UmicomU64)~(UmicomU32)0U << 32U) | 1U;
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 11U, final), UMICOM_MESSAGE_OK, name);
            UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 22U, umicomTestRight, 11U, 0U, &output), UMICOM_MESSAGE_OK, "retired slot skipped");
            UmicomTestRequire((UmicomU32)output != 1U && UmicomTestSnapshot().retiredHandles == 1U, "never wrap old generation");
        } else if (UmicomTestIs(name, "sequence-exhaustion")) {
            /* Exhaustion injection: the final nonzero sequence remains usable. */
            umicomTestDomain.channels[0].nextSequence = ~(UmicomU64)0U;
            UmicomTestSend(6U);
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 1U), UMICOM_MESSAGE_SEQUENCE_EXHAUSTED, name);
            UmicomTestRequire(UmicomTestReceive().sequence == ~(UmicomU64)0U, "final sequence issued once");
        } else if (UmicomTestIs(name, "owner-quota") || UmicomTestIs(name, "same-owner-pair-quota")) {
            const UmicomSize end = UmicomTestIs(name, "same-owner-pair-quota") ? 7U : 8U;
            for (UmicomSize count = 1U; count < end; ++count) {
                UmicomTestExpect(UmicomKernelMessageDuplicate(&umicomTestDomain, 11U, umicomTestLeft, 0U, &output), UMICOM_MESSAGE_OK, "fill owner count");
            }
            if (UmicomTestIs(name, "same-owner-pair-quota")) {
                UmicomKernelMessageHandle second = 0U;
                UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestDomain, 11U, 0U, 11U, 0U, &output, &second), UMICOM_MESSAGE_OWNER_LIMIT, name);
                UmicomTestRequire(second == 0U, "pair preflight consumes two slots");
            } else UmicomTestExpect(UmicomKernelMessageDuplicate(&umicomTestDomain, 11U, umicomTestLeft, 0U, &output), UMICOM_MESSAGE_OWNER_LIMIT, name);
        } else if (UmicomTestIs(name, "handle-capacity")) {
            for (UmicomU64 owner = 100U; owner < 130U; ++owner) {
                UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 11U, umicomTestLeft, owner, 0U, &output), UMICOM_MESSAGE_OK, "fill handle table");
            }
            UmicomTestExpect(UmicomKernelMessageGrant(&umicomTestDomain, 11U, umicomTestLeft, 130U, 0U, &output), UMICOM_MESSAGE_HANDLE_LIMIT, name);
        } else if (UmicomTestIs(name, "separate-domains")) {
            UmicomTestExpect(UmicomKernelMessageInitialize(&umicomTestOther), UMICOM_MESSAGE_OK, "other domain");
            UmicomKernelMessageHandle a, b;
            UmicomTestExpect(UmicomKernelMessagePairCreate(&umicomTestOther, 11U, UMICOM_MESSAGE_RIGHT_ALL, 22U, UMICOM_MESSAGE_RIGHT_ALL, &a, &b), UMICOM_MESSAGE_OK, "same token encoding in separate domain");
            UmicomTestRequire(a == umicomTestLeft, "domain is part of token interpretation");
            UmicomTestSend(4U);
            UmicomTestExpect(UmicomKernelMessageReceive(&umicomTestOther, 22U, b, &message, 256U), UMICOM_MESSAGE_WOULD_BLOCK, name);
            UmicomTestTeardownDomain(&umicomTestOther);
        } else if (UmicomTestIs(name, "corrupt-reference-count")) {
            ++umicomTestDomain.channels[0].references[0]; /* Labelled corruption injection. */
            UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_CORRUPT_STATE, name);
            --umicomTestDomain.channels[0].references[0];
        } else if (UmicomTestIs(name, "corrupt-message-length") || UmicomTestIs(name, "corrupt-sequence")) {
            UmicomTestSend(4U);
            UmicomKernelMessage *const queued = &umicomTestDomain.channels[0].incoming[1].slots[0];
            if (UmicomTestIs(name, "corrupt-message-length")) queued->bytes = 257U;
            else queued->sequence = 0U;
            UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_CORRUPT_STATE, name);
            queued->bytes = 1U; queued->sequence = 1U; /* Undo only the injected corruption. */
        } else if (UmicomTestIs(name, "corrupt-boolean")) {
            umicomTestDomain.channels[1].occupied = (UmicomBoolean)2U; /* Corruption injection. */
            UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_CORRUPT_STATE, name);
            umicomTestDomain.channels[1].occupied = UMICOM_FALSE;
        } else if (UmicomTestIs(name, "corrupt-empty-channel")) {
            umicomTestDomain.channels[1].incoming[0].slots[0].data[7] = 1U; /* Corruption injection. */
            UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_CORRUPT_STATE, name);
            umicomTestDomain.channels[1].incoming[0].slots[0].data[7] = 0U;
        } else if (UmicomTestIs(name, "corrupt-padding")) {
            UmicomTestSend(3U);
            umicomTestDomain.channels[0].incoming[1].slots[0].data[1] = 1U; /* Beyond the one-byte payload. */
            UmicomTestExpect(UmicomKernelMessageValidate(&umicomTestDomain), UMICOM_MESSAGE_CORRUPT_STATE, name);
            umicomTestDomain.channels[0].incoming[1].slots[0].data[1] = 0U;
        } else if (UmicomTestIs(name, "scrub-consumed") || UmicomTestIs(name, "scrub-recycled")) {
            UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 256U), UMICOM_MESSAGE_OK, "fill complete packet");
            (void)UmicomTestReceive();
            const UmicomU8 *const retired = (const UmicomU8 *)&umicomTestDomain.channels[0].incoming[1].slots[0];
            for (UmicomSize byte = 0U; byte < sizeof(message); ++byte) UmicomTestRequire(retired[byte] == 0U, "consumed storage scrubbed");
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 11U, umicomTestLeft), UMICOM_MESSAGE_OK, "close left");
            UmicomTestExpect(UmicomKernelMessageClose(&umicomTestDomain, 22U, umicomTestRight), UMICOM_MESSAGE_OK, "close right");
            const UmicomU8 *const channel = (const UmicomU8 *)&umicomTestDomain.channels[0];
            for (UmicomSize byte = 0U; byte < sizeof(umicomTestDomain.channels[0]); ++byte) UmicomTestRequire(channel[byte] == 0U, name);
        } else if (UmicomTestIs(name, "repeated-lifetimes")) {
            UmicomTestTeardownDomain(&umicomTestDomain);
            UmicomKernelMessageHandle stale = umicomTestLeft;
            for (UmicomSize iteration = 0U; iteration < 2000U; ++iteration) {
                UmicomTestPair(UMICOM_MESSAGE_RIGHT_ALL, UMICOM_MESSAGE_RIGHT_ALL);
                UmicomTestExpect(UmicomKernelMessageQuery(&umicomTestDomain, 11U, stale, &info), UMICOM_MESSAGE_INVALID_HANDLE, "old slot never resurrected");
                for (UmicomU8 byte = 0U; byte < 4U; ++byte) UmicomTestSend(byte);
                for (UmicomU8 byte = 0U; byte < 4U; ++byte) UmicomTestRequire(UmicomTestReceive().data[0] == byte, "FIFO after reuse");
                stale = umicomTestLeft;
                UmicomTestTeardownDomain(&umicomTestDomain);
            }
        } else {
            /* The remaining cases execute the real C trap dispatcher with
             * actual checked page mappings, not a fake byte-copy callback. */
            UmicomTestMapPrepare();
            if (UmicomTestIs(name, "binding-unbound")) {
                UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestLeft, UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_SERVICE_UNBOUND, name);
            } else {
                const UmicomBoolean receiver = strstr(name, "receive") != NULL || UmicomTestIs(name, "syscall-too-small") ||
                    UmicomTestIs(name, "syscall-readonly") || UmicomTestIs(name, "syscall-empty")
                    ? UMICOM_TRUE : UMICOM_FALSE;
                UmicomTestBind(receiver != UMICOM_FALSE ? 22U : 11U);
                if (UmicomTestIs(name, "binding-nested")) {
                    UmicomTestExpect(UmicomKernelMessageServiceBind(&umicomTestDomain, 22U), UMICOM_MESSAGE_BAD_STATE, name);
                } else if (UmicomTestIs(name, "binding-wrong-identity")) {
                    umicomTestSession.identity = 22U;
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestRight, UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_SERVICE_UNBOUND, name);
                    umicomTestSession.identity = 11U;
                } else if (UmicomTestIs(name, "binding-wrong-unbind")) {
                    UmicomTestExpect(UmicomKernelMessageServiceUnbind(&umicomTestDomain, 22U), UMICOM_MESSAGE_BAD_STATE, name);
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestLeft, UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_OK, "original binding survives");
                } else if (UmicomTestIs(name, "syscall-send") || UmicomTestIs(name, "syscall-register-return") || UmicomTestIs(name, "syscall-cross-page-send")) {
                    const UmicomAddress source = UMICOM_TEST_DATA + 4096U - 16U;
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_SEND, umicomTestLeft, source, 256U), UMICOM_MESSAGE_OK, name);
                    message = UmicomTestReceive();
                    UmicomTestRequire(message.sender == 11U && message.bytes == 256U, "authenticated session identity");
                    for (UmicomSize byte = 0U; byte < 256U; ++byte) UmicomTestRequire(message.data[byte] == 0xccU, "non-contiguous physical read");
                } else if (UmicomTestIs(name, "syscall-receive") || UmicomTestIs(name, "syscall-cross-page-receive")) {
                    UmicomTestExpect(UmicomKernelMessageSend(&umicomTestDomain, 11U, umicomTestLeft, payload, 256U), UMICOM_MESSAGE_OK, "enqueue full payload");
                    const UmicomAddress destination = UMICOM_TEST_DATA + 4096U - 16U;
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, umicomTestRight, destination, 280U), UMICOM_MESSAGE_OK, name);
                    UmicomU8 result[280];
                    for (UmicomSize offset = 0U; offset < sizeof(result); offset += 40U) {
                        UmicomTestRequire(UmicomKernelUserMemoryRead(&umicomTestSession.memory, destination + offset, result + offset, 40U) == UMICOM_USER_RESULT_OK, "read back packet chunks");
                    }
                    memcpy(&message, result, sizeof(message));
                    UmicomTestRequire(message.sender == 11U && message.sequence == 1U && message.bytes == 256U && memcmp(message.data, payload, 256U) == 0, "packet metadata and payload");
                    UmicomTestRequire(UmicomTestSnapshot().messages == 0U, "consumed only after delivery");
                } else if (UmicomTestIs(name, "syscall-owner-refusal")) {
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestRight, UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_WRONG_OWNER, name);
                } else if (UmicomTestIs(name, "syscall-invalid-pc") || UmicomTestIs(name, "syscall-machine-trap") || UmicomTestIs(name, "syscall-call-budget")) {
                    UmicomRiscvTrapFrame frame = UmicomTestFrame(UMICOM_USER_CALL_MESSAGE_SEND, umicomTestLeft, UMICOM_TEST_DATA, 1U);
                    if (UmicomTestIs(name, "syscall-invalid-pc")) frame.mepc = 0U;
                    if (UmicomTestIs(name, "syscall-machine-trap")) frame.mstatus = 0x1800U;
                    if (UmicomTestIs(name, "syscall-call-budget")) umicomTestSession.callCount = UMICOM_USER_CALL_LIMIT;
                    UmicomTestRequire(UmicomKernelUserTrapDispatch(&umicomTestSession, &frame) == 0U && UmicomTestSnapshot().messages == 0U, name);
                } else if (UmicomTestIs(name, "syscall-send-gap")) {
                    UmicomTestRequire(UmicomKernelVirtualMemoryUnmapPage(&umicomTestSpace, UMICOM_TEST_DATA + 4096U) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "remove second source leaf");
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_SEND, umicomTestLeft,
                        UMICOM_TEST_DATA + 4096U - 16U, 32U), UMICOM_MESSAGE_BAD_USER_BUFFER, name);
                    UmicomTestRequire(UmicomTestSnapshot().messages == 0U, "source refusal did not enqueue");
                } else if (UmicomTestIs(name, "syscall-receive-gap") || UmicomTestIs(name, "syscall-readonly")) {
                    UmicomTestSend(5U);
                    UmicomTestRequire(UmicomKernelVirtualMemoryUnmapPage(&umicomTestSpace, UMICOM_TEST_DATA + 4096U) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "remove destination leaf");
                    if (UmicomTestIs(name, "syscall-readonly")) {
                        umicomTestPages[2].permissions = UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_USER;
                        UmicomTestRequire(UmicomKernelVirtualMemoryMapPage(&umicomTestSpace, UMICOM_TEST_DATA + 4096U,
                            umicomTestBacking[3], umicomTestPages[2].permissions) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "read-only destination page");
                    }
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, umicomTestRight,
                        UMICOM_TEST_DATA + 4096U - 16U, 280U), UMICOM_MESSAGE_BAD_USER_BUFFER, name);
                    for (UmicomSize byte = 0U; byte < 4096U; ++byte) UmicomTestRequire(((const UmicomU8 *)umicomTestBacking[1])[byte] == 0xccU, "no partial first-page write");
                    UmicomTestRequire(UmicomTestSnapshot().messages == 1U, "invalid destination did not consume");
                } else if (UmicomTestIs(name, "syscall-overflow")) {
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_SEND, umicomTestLeft,
                        ~(UmicomAddress)0U - 7U, 32U), UMICOM_MESSAGE_BAD_USER_BUFFER, name);
                } else if (UmicomTestIs(name, "syscall-too-small")) {
                    UmicomTestSend(6U);
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, umicomTestRight,
                        UMICOM_TEST_DATA, 24U), UMICOM_MESSAGE_BUFFER_TOO_SMALL, name);
                    UmicomTestRequire(UmicomTestSnapshot().messages == 1U && *(const UmicomU8 *)umicomTestBacking[1] == 0xccU, "undersized receive unchanged");
                } else if (UmicomTestIs(name, "syscall-empty")) {
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, umicomTestRight,
                        UMICOM_TEST_DATA, 280U), UMICOM_MESSAGE_WOULD_BLOCK, name);
                } else if (UmicomTestIs(name, "syscall-query")) {
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestLeft,
                        UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_OK, name);
                    memcpy(&info, (const void *)umicomTestBacking[1], sizeof(info));
                    UmicomTestRequire(info.rights == UMICOM_MESSAGE_RIGHT_ALL && info.queued == 0U && info.flags == UMICOM_MESSAGE_WRITABLE, "fixed query layout");
                } else if (UmicomTestIs(name, "syscall-close")) {
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_CLOSE, umicomTestLeft, 0U, 0U), UMICOM_MESSAGE_OK, name);
                    UmicomTestExpect(UmicomTestCall(UMICOM_USER_CALL_MESSAGE_QUERY, umicomTestLeft, UMICOM_TEST_DATA, 0U), UMICOM_MESSAGE_INVALID_HANDLE, "closed syscall token");
                } else if (UmicomTestIs(name, "syscall-unknown")) {
                    UmicomTestRequire((UmicomU64)UmicomTestCall(999U, 0U, 0U, 0U) == UMICOM_USER_RESULT_UNKNOWN_CALL, name);
                } else UmicomTestRequire(0, "unknown test name");
            }
        }
    }
    UmicomTestTeardown();
    printf("PASS %s (%u checks)\n", name, umicomTestChecks);
    return EXIT_SUCCESS;
}
