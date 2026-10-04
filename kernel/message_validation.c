/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/message_validation.c
 *
 * PURPOSE:
 *   Prove queue ownership in the Kernel, then exchange messages through actual
 *   ECALLs from two separate process-registry images of a native ELF program.
 *
 * EDUCATIONAL OVERVIEW:
 *   The sender finishes before the receiver runs because there is no scheduler
 *   yet. Copied packets bridge those two lifetimes without sharing page tables
 *   or borrowing the exited sender's memory. A closed peer remains readable
 *   until its already accepted messages have been drained.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/message_service.h"
#include "umicom/kernel/process_registry.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

extern const UmicomU8 UmicomMessageExecutableStart[];
extern const UmicomU8 UmicomMessageExecutableEnd[];
extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
static UmicomKernelMessageDomain umicomMessageAcceptanceDomain;
static UmicomKernelProcessRegistry umicomMessageAcceptanceProcesses;
static UmicomSize umicomMessageAcceptanceChecks;
static UmicomSize umicomMessageAcceptanceCases;

static void UmicomMessageRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomMessageAcceptanceChecks;
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=message-");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(220U);
        UmicomPlatformHalt();
    }
}
static void UmicomMessageExpect(UmicomKernelMessageStatus actual,
    UmicomKernelMessageStatus expected, const char *reason)
{
    if (actual != expected) {
        UmicomKernelConsoleWrite("message.unexpected-status=");
        UmicomKernelConsoleWriteLine(UmicomKernelMessageStatusName(actual));
    }
    UmicomMessageRequire(actual == expected ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomMessageRegistryExpect(UmicomKernelRegistryStatus status, const char *reason)
{
    if (status != UMICOM_REGISTRY_OK) {
        UmicomKernelConsoleWrite("message.registry-status=");
        UmicomKernelConsoleWriteLine(UmicomKernelRegistryStatusName(status));
    }
    UmicomMessageRequire(status == UMICOM_REGISTRY_OK ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomMessageNumber(const char *name, UmicomU64 value)
{
    UmicomKernelConsoleWrite(name);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static void UmicomMessageCase(const char *name)
{
    ++umicomMessageAcceptanceCases;
    UmicomKernelConsoleWrite("message.case=");
    UmicomKernelConsoleWriteLine(name);
}
static void UmicomMessageEmpty(void)
{
    UmicomKernelMessageSnapshot snapshot;
    UmicomMessageExpect(UmicomKernelMessageSnapshotRead(&umicomMessageAcceptanceDomain, &snapshot),
        UMICOM_MESSAGE_OK, "independent-queue-recount");
    UmicomMessageRequire(snapshot.channels == 0U && snapshot.handles == 0U && snapshot.messages == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "queues-and-references-reclaimed");
}
static UmicomKernelProcessHandle UmicomMessageLoad(UmicomU64 owner, UmicomBoolean diagnostic)
{
    const UmicomU8 *const start = diagnostic != UMICOM_FALSE ? UmicomEmbeddedExecutableStart : UmicomMessageExecutableStart;
    const UmicomU8 *const end = diagnostic != UMICOM_FALSE ? UmicomEmbeddedExecutableEnd : UmicomMessageExecutableEnd;
    const UmicomAddress startAddress = (UmicomAddress)start;
    const UmicomAddress endAddress = (UmicomAddress)end;
    UmicomMessageRequire(endAddress > startAddress ? UMICOM_TRUE : UMICOM_FALSE, "embedded-elf-range");
    UmicomKernelProcessHandle handle = 0U;
    UmicomMessageRegistryExpect(UmicomKernelProcessRegistryCreate(&umicomMessageAcceptanceProcesses,
        owner, start, (UmicomSize)(endAddress - startAddress),
        UMICOM_PROCESS_RIGHT_QUERY | UMICOM_PROCESS_RIGHT_RUN, &handle), "load-registered-program");
    return handle;
}
static UmicomKernelProcessInfo UmicomMessageProcessInfo(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomKernelProcessInfo info;
    UmicomMessageRegistryExpect(UmicomKernelProcessRegistryQuery(&umicomMessageAcceptanceProcesses,
        owner, handle, &info), "inspect-registered-program");
    return info;
}
static UmicomKernelProcessInfo UmicomMessageRun(UmicomU64 owner,
    UmicomKernelProcessHandle process, UmicomU64 argument, UmicomBoolean bind)
{
    const UmicomKernelProcessInfo before = UmicomMessageProcessInfo(owner, process);
    if (bind != UMICOM_FALSE) {
        UmicomMessageExpect(UmicomKernelMessageServiceBind(&umicomMessageAcceptanceDomain, before.identity),
            UMICOM_MESSAGE_OK, "bind-authenticated-process-identity");
    }
    const UmicomKernelRegistryStatus status = UmicomKernelProcessRegistryRun(&umicomMessageAcceptanceProcesses,
        owner, process, argument, 2000000U);
    /* Remove the service even if the invocation faulted or reached its deadline.
     * The assertion comes after unbinding so an unexpected run result cannot
     * leave authority installed for the next program. */
    if (bind != UMICOM_FALSE) {
        UmicomMessageExpect(UmicomKernelMessageServiceUnbind(&umicomMessageAcceptanceDomain, before.identity),
            UMICOM_MESSAGE_OK, "unbind-terminal-invocation");
    }
    UmicomMessageRegistryExpect(status, "run-through-established-monitor");
    const UmicomKernelProcessInfo after = UmicomMessageProcessInfo(owner, process);
    UmicomMessageNumber("message.process.identity", after.identity);
    UmicomMessageNumber("message.process.exit-value", after.exitValue);
    UmicomMessageNumber("message.process.system-calls", after.systemCalls);
    UmicomMessageRequire(after.quiesced == UMICOM_TRUE ? UMICOM_TRUE : UMICOM_FALSE, "machine-return-quiesced");
    return after;
}
static void UmicomMessageDestroy(UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomMessageRegistryExpect(UmicomKernelProcessRegistryClose(&umicomMessageAcceptanceProcesses,
        owner, handle), "explicit-process-destruction");
}
void UmicomKernelMessageChannelsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("message-channels-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomMessageRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "physical-baseline");
    UmicomMessageExpect(UmicomKernelMessageInitialize(&umicomMessageAcceptanceDomain), UMICOM_MESSAGE_OK, "initialise-domain");
    UmicomMessageRegistryExpect(UmicomKernelProcessRegistryInitialize(&umicomMessageAcceptanceProcesses), "initialise-process-owner");

    UmicomMessageCase("rights-grants-and-copied-lifetime");
    UmicomKernelMessageHandle left = 0U;
    UmicomKernelMessageHandle right = 0U;
    UmicomKernelMessageHandle observer = 0U;
    UmicomMessageExpect(UmicomKernelMessagePairCreate(&umicomMessageAcceptanceDomain,
        100U, UMICOM_MESSAGE_RIGHT_ALL, 200U, UMICOM_MESSAGE_RIGHT_ALL, &left, &right), UMICOM_MESSAGE_OK, "paired-endpoints");
    UmicomKernelMessageInfo info;
    UmicomMessageExpect(UmicomKernelMessageQuery(&umicomMessageAcceptanceDomain, 200U, left, &info),
        UMICOM_MESSAGE_WRONG_OWNER, "copied-token-is-not-authority");
    UmicomMessageExpect(UmicomKernelMessageGrant(&umicomMessageAcceptanceDomain, 200U, right, 300U,
        UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY, &observer), UMICOM_MESSAGE_OK, "attenuated-grant");
    UmicomMessageExpect(UmicomKernelMessageRestrict(&umicomMessageAcceptanceDomain, 300U, observer,
        UMICOM_MESSAGE_RIGHT_ALL), UMICOM_MESSAGE_ACCESS_DENIED, "no-rights-escalation");
    UmicomU8 payload[3] = { 11U, 22U, 33U };
    UmicomMessageExpect(UmicomKernelMessageSend(&umicomMessageAcceptanceDomain, 100U, left, payload, sizeof(payload)),
        UMICOM_MESSAGE_OK, "send-copied-bytes");
    payload[0] = 99U;
    UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 200U, right), UMICOM_MESSAGE_OK, "close-one-receiving-reference");
    UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 100U, left), UMICOM_MESSAGE_OK, "close-sender-with-message-pending");
    UmicomKernelMessage message;
    UmicomMessageExpect(UmicomKernelMessageReceive(&umicomMessageAcceptanceDomain, 300U, observer,
        &message, sizeof(message.data)), UMICOM_MESSAGE_OK, "surviving-peer-drains-accepted-message");
    UmicomMessageRequire(message.sender == 100U && message.sequence == 1U && message.bytes == 3U && message.data[0] == 11U
        ? UMICOM_TRUE : UMICOM_FALSE, "metadata-and-source-copy");
    UmicomMessageExpect(UmicomKernelMessageReceive(&umicomMessageAcceptanceDomain, 300U, observer,
        &message, sizeof(message.data)), UMICOM_MESSAGE_PEER_CLOSED, "drained-closed-peer");
    UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 300U, observer), UMICOM_MESSAGE_OK, "last-reference-reclaimed");
    UmicomMessageExpect(UmicomKernelMessageQuery(&umicomMessageAcceptanceDomain, 100U, left, &info), UMICOM_MESSAGE_INVALID_HANDLE, "stale-reference");
    UmicomMessageEmpty();
    UmicomKernelConsoleWriteLine("message.case-result=pass");

    UmicomMessageCase("duplex-queues-and-receiver-close");
    UmicomMessageExpect(UmicomKernelMessagePairCreate(&umicomMessageAcceptanceDomain,
        400U, UMICOM_MESSAGE_RIGHT_ALL, 500U, UMICOM_MESSAGE_RIGHT_ALL, &left, &right), UMICOM_MESSAGE_OK, "new-pair");
    UmicomMessageExpect(UmicomKernelMessageSend(&umicomMessageAcceptanceDomain, 400U, left, payload, 1U), UMICOM_MESSAGE_OK, "send-left");
    UmicomMessageExpect(UmicomKernelMessageSend(&umicomMessageAcceptanceDomain, 500U, right, payload, 2U), UMICOM_MESSAGE_OK, "send-right");
    UmicomMessageExpect(UmicomKernelMessageReceive(&umicomMessageAcceptanceDomain, 400U, left, &message, 256U), UMICOM_MESSAGE_OK, "independent-incoming-queue");
    UmicomMessageRequire(message.sender == 500U && message.sequence == 2U && message.bytes == 2U ? UMICOM_TRUE : UMICOM_FALSE, "duplex-origin");
    UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 500U, right), UMICOM_MESSAGE_OK, "discard-dead-end-incoming");
    UmicomMessageExpect(UmicomKernelMessageSend(&umicomMessageAcceptanceDomain, 400U, left, payload, 1U), UMICOM_MESSAGE_PEER_CLOSED, "no-send-to-dead-peer");
    UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 400U, left), UMICOM_MESSAGE_OK, "close-other-end");
    UmicomMessageEmpty();
    UmicomKernelConsoleWriteLine("message.case-result=pass");

    UmicomMessageCase("loaded-producer-and-consumer-system-calls");
    const UmicomKernelProcessHandle producer = UmicomMessageLoad(9001U, UMICOM_FALSE);
    const UmicomKernelProcessHandle consumer = UmicomMessageLoad(9002U, UMICOM_FALSE);
    const UmicomU64 producerIdentity = UmicomMessageProcessInfo(9001U, producer).identity;
    const UmicomU64 consumerIdentity = UmicomMessageProcessInfo(9002U, consumer).identity;
    UmicomMessageRequire(producerIdentity != consumerIdentity ? UMICOM_TRUE : UMICOM_FALSE, "separate-process-identities");
    UmicomMessageExpect(UmicomKernelMessagePairCreate(&umicomMessageAcceptanceDomain,
        producerIdentity, UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY,
        consumerIdentity, UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY, &left, &right),
        UMICOM_MESSAGE_OK, "bind-endpoints-to-process-identities");
    UmicomKernelProcessInfo terminal = UmicomMessageRun(9001U, producer, left, UMICOM_TRUE);
    UmicomMessageRequire(terminal.state == UMICOM_PROCESS_EXITED && terminal.exitValue == 0x5100U + producerIdentity
        ? UMICOM_TRUE : UMICOM_FALSE, "producer-user-validation");
    /* Destroy the sender's actual pages before the receiver runs. A borrowed
     * sender pointer would now be invalid; the copied queue must remain valid. */
    UmicomMessageDestroy(9001U, producer);
    UmicomMessageExpect(UmicomKernelMessageQuery(&umicomMessageAcceptanceDomain, consumerIdentity, right, &info), UMICOM_MESSAGE_OK, "queued-after-sender-destruction");
    UmicomMessageRequire(info.queued == 4U && (info.flags & UMICOM_MESSAGE_PEER_GONE) != 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "closed-sender-still-readable");
    terminal = UmicomMessageRun(9002U, consumer, right, UMICOM_TRUE);
    UmicomMessageRequire(terminal.state == UMICOM_PROCESS_EXITED && terminal.exitValue == 0x5200U + producerIdentity * 16U + consumerIdentity
        ? UMICOM_TRUE : UMICOM_FALSE, "consumer-user-validation");
    UmicomMessageDestroy(9002U, consumer);
    UmicomMessageEmpty();
    UmicomKernelConsoleWriteLine("message.user-exchange=pass");
    UmicomKernelConsoleWriteLine("message.case-result=pass");

    UmicomMessageCase("unbound-process-has-no-message-domain");
    const UmicomKernelProcessHandle unbound = UmicomMessageLoad(9003U, UMICOM_FALSE);
    terminal = UmicomMessageRun(9003U, unbound, right, UMICOM_FALSE);
    UmicomMessageRequire(terminal.state == UMICOM_PROCESS_EXITED && terminal.exitValue == 0xea02U
        ? UMICOM_TRUE : UMICOM_FALSE, "unbound-service-refused");
    UmicomMessageDestroy(9003U, unbound);
    UmicomMessageEmpty();
    UmicomKernelConsoleWriteLine("message.case-result=pass");

    for (UmicomU64 operation = 2U; operation <= 3U; ++operation) {
        UmicomMessageCase(operation == 2U ? "fault-owner-cleanup" : "deadline-owner-cleanup");
        const UmicomKernelProcessHandle stopped = UmicomMessageLoad(9004U, UMICOM_TRUE);
        const UmicomU64 stoppedIdentity = UmicomMessageProcessInfo(9004U, stopped).identity;
        UmicomMessageExpect(UmicomKernelMessagePairCreate(&umicomMessageAcceptanceDomain,
            stoppedIdentity, UMICOM_MESSAGE_RIGHT_ALL, 8000U, UMICOM_MESSAGE_RIGHT_ALL, &left, &right), UMICOM_MESSAGE_OK, "terminal-test-endpoint");
        terminal = UmicomMessageRun(9004U, stopped, operation, UMICOM_TRUE);
        UmicomMessageRequire(terminal.state == (operation == 2U ? UMICOM_PROCESS_FAULTED : UMICOM_PROCESS_TIMED_OUT)
            ? UMICOM_TRUE : UMICOM_FALSE, "terminal-state-preserved");
        UmicomSize closed = 0U;
        UmicomMessageExpect(UmicomKernelMessageCloseOwner(&umicomMessageAcceptanceDomain, stoppedIdentity, &closed), UMICOM_MESSAGE_OK, "explicit-owner-cleanup");
        UmicomMessageRequire(closed == 1U ? UMICOM_TRUE : UMICOM_FALSE, "cleanup-reference-count");
        UmicomMessageExpect(UmicomKernelMessageClose(&umicomMessageAcceptanceDomain, 8000U, right), UMICOM_MESSAGE_OK, "surviving-peer-close");
        UmicomMessageDestroy(9004U, stopped);
        UmicomMessageEmpty();
        UmicomKernelConsoleWriteLine("message.case-result=pass");
    }
    UmicomKernelRegistrySnapshot processes;
    UmicomMessageRegistryExpect(UmicomKernelProcessRegistrySnapshotRead(&umicomMessageAcceptanceProcesses, &processes), "final-process-recount");
    UmicomMessageRequire(processes.objects == 0U && processes.handles == 0U && processes.retainedWithoutHandles == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "no-retained-process");
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomMessageRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U && trapAfter.lastCauseCode == 11U
        ? UMICOM_TRUE : UMICOM_FALSE, "original-trap-handler");
    UmicomKernelConsoleWriteLine("message.original-trap-handler=pass");
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomMessageRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        after.allocatedFrames == before.allocatedFrames && after.freeFrames == before.freeFrames &&
        after.reservedFrames == before.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "physical-frame-accounting");
    UmicomKernelConsoleWriteLine("message.frame-accounting=restored");
    UmicomMessageNumber("message.completed-cases", umicomMessageAcceptanceCases);
    UmicomMessageNumber("message.completed-checks", umicomMessageAcceptanceChecks);
    UmicomKernelConsoleWriteLine("message-channels-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_MESSAGE_CHANNELS_READY");
}
