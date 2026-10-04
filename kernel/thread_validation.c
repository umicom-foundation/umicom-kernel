/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/thread_validation.c
 *
 * PURPOSE:
 *   Exercise real RV64 cooperative stacks, register preservation, time-driven
 *   wake-up and copied message queues, then return to the established Kernel.
 *
 * EDUCATIONAL OVERVIEW:
 *   These are trusted Kernel callbacks, not new user syscalls. The producer and
 *   consumer use the existing channel API, but now retain their C locals across
 *   a wait while a peer runs. Neither channel implementation nor user monitor
 *   changes. Explicit wake calls demonstrate the serialised condition/wait
 *   protocol rather than claiming atomic multi-hart blocking IPC.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/threads.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/message_channel.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"

UmicomBoolean UmicomRiscvThreadRegistersProbe(UmicomKernelScheduler *scheduler, UmicomU64 seed);
/* Static stacks lie inside the Kernel BSS reservation already protected by the
 * physical allocator. This test neither allocates nor frees unrelated frames. */
static UmicomKernelScheduler umicomThreadScheduler;
static UmicomKernelMessageDomain umicomThreadMessages;
static UmicomU64 umicomThreadTrace[32];
static UmicomSize umicomThreadTraceCount;
static UmicomSize umicomThreadChecks;
static UmicomSize umicomThreadCases;

typedef struct UmicomThreadWork {
    UmicomU64 identity;
    UmicomU64 progress;
    UmicomAddress observedLocal;
    UmicomU64 deadline;
} UmicomThreadWork;

static void UmicomThreadRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomThreadChecks;
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=threads-");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(230U);
        UmicomPlatformHalt();
    }
}
static void UmicomThreadExpect(UmicomKernelThreadStatus status, const char *reason)
{
    if (status != UMICOM_THREAD_OK) {
        UmicomKernelConsoleWrite("threads.unexpected-status=");
        UmicomKernelConsoleWriteLine(UmicomKernelThreadStatusName(status));
    }
    UmicomThreadRequire(status == UMICOM_THREAD_OK ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomThreadNumber(const char *name, UmicomU64 value)
{
    UmicomKernelConsoleWrite(name);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static void UmicomThreadCase(const char *name)
{
    ++umicomThreadCases;
    UmicomKernelConsoleWrite("threads.case=");
    UmicomKernelConsoleWriteLine(name);
}
static void UmicomThreadDrain(void)
{
    /* This bounds the number of cooperative dispatches. It cannot stop a
     * trusted callback that refuses to yield; CTest supplies the outer timeout. */
    for (UmicomSize dispatch = 0U; dispatch < 256U; ++dispatch) {
        const UmicomKernelThreadStatus status = UmicomKernelSchedulerRunOne(&umicomThreadScheduler);
        if (status == UMICOM_THREAD_IDLE) return;
        UmicomThreadExpect(status, "dispatch");
    }
    UmicomThreadRequire(UMICOM_FALSE, "dispatch-budget");
}
static UmicomKernelThreadInfo UmicomThreadCollect(UmicomKernelThreadHandle handle)
{
    UmicomKernelThreadInfo info;
    UmicomThreadExpect(UmicomKernelThreadReap(&umicomThreadScheduler, handle, &info), "reap-terminal-stack");
    return info;
}
static UmicomU64 UmicomThreadNested(UmicomThreadWork *work, UmicomSize depth)
{
    /* Volatile locals force real stack storage at every recursion level. The
     * separate Assembly probe tests callee-saved registers independently. */
    volatile UmicomU64 values[8];
    for (UmicomSize index = 0U; index < 8U; ++index) values[index] = work->identity + depth + index;
    if (depth != 0U) {
        UmicomThreadRequire(UmicomThreadNested(work, depth - 1U) == work->identity ? UMICOM_TRUE : UMICOM_FALSE,
            "nested-return-value");
    } else {
        work->observedLocal = (UmicomAddress)&values[0];
        for (UmicomSize round = 0U; round < 4U; ++round) {
            UmicomThreadRequire(umicomThreadTraceCount < 32U ? UMICOM_TRUE : UMICOM_FALSE, "trace-capacity");
            umicomThreadTrace[umicomThreadTraceCount++] = work->identity;
            ++work->progress;
            UmicomThreadExpect(UmicomKernelThreadYield(&umicomThreadScheduler), "resume-nested-stack");
        }
    }
    for (UmicomSize index = 0U; index < 8U; ++index)
        UmicomThreadRequire(values[index] == work->identity + depth + index ? UMICOM_TRUE : UMICOM_FALSE,
            "stack-local-preserved");
    return work->identity;
}
static UmicomU64 UmicomThreadRoundRobin(void *argument)
{
    UmicomThreadWork *const work = (UmicomThreadWork *)argument;
    /* Different seeds ensure a missing saved-register slot cannot be masked
     * by another callback leaving the same value in that CPU register. */
    UmicomThreadRequire(UmicomRiscvThreadRegistersProbe(&umicomThreadScheduler, work->identity * 100U),
        "callee-saved-registers");
    return UmicomThreadNested(work, 3U);
}
static UmicomU64 UmicomThreadSleeper(void *argument)
{
    UmicomThreadWork *const work = (UmicomThreadWork *)argument;
    ++work->progress;
    UmicomThreadExpect(UmicomKernelThreadSleepUntil(&umicomThreadScheduler, work->deadline), "sleep-until");
    ++work->progress;
    return work->identity;
}
static UmicomU64 UmicomThreadWaiter(void *argument)
{
    UmicomThreadWork *const work = (UmicomThreadWork *)argument;
    ++work->progress;
    UmicomThreadExpect(UmicomKernelThreadWait(&umicomThreadScheduler), "explicit-wait");
    ++work->progress;
    return work->identity;
}
/* This callback deliberately never completes. Its owner cancels it only
 * while it is suspended on another stack; the function still uses the common
 * callback return type so it can be admitted through the same typed API. */
_Noreturn static UmicomU64 UmicomThreadYieldForever(void *argument)
{
    UmicomThreadWork *const work = (UmicomThreadWork *)argument;
    for (;;) {
        ++work->progress;
        UmicomThreadExpect(UmicomKernelThreadYield(&umicomThreadScheduler), "cooperative-loop");
    }
}

typedef struct UmicomThreadExchange {
    UmicomKernelThreadHandle producerThread, consumerThread;
    UmicomKernelMessageHandle producerEndpoint, consumerEndpoint;
    UmicomSize sent, received, waits;
} UmicomThreadExchange;
static UmicomU64 UmicomThreadProducer(void *argument)
{
    UmicomThreadExchange *const exchange = (UmicomThreadExchange *)argument;
    UmicomU8 payload[32];
    for (UmicomSize packet = 0U; packet < 24U; ++packet) {
        for (UmicomSize byte = 0U; byte < 32U; ++byte) payload[byte] = (UmicomU8)(packet + byte);
        for (;;) {
            const UmicomKernelMessageStatus status = UmicomKernelMessageSend(&umicomThreadMessages,
                701U, exchange->producerEndpoint, payload, 32U);
            if (status == UMICOM_MESSAGE_OK) break;
            UmicomThreadRequire(status == UMICOM_MESSAGE_WOULD_BLOCK ? UMICOM_TRUE : UMICOM_FALSE, "send-status");
            ++exchange->waits;
            /* No switch occurs between observing a full queue and Wait. The
             * consumer cannot drain and wake us in that serialised interval. */
            UmicomThreadExpect(UmicomKernelThreadWait(&umicomThreadScheduler), "producer-wait");
        }
        ++exchange->sent;
        /* Queue ownership must outlive and differ from this reusable stack buffer. */
        for (UmicomSize byte = 0U; byte < 32U; ++byte) payload[byte] = (UmicomU8)0xeeU;
        UmicomThreadExpect(UmicomKernelThreadWake(&umicomThreadScheduler, exchange->consumerThread), "wake-consumer");
    }
    UmicomThreadRequire(UmicomKernelMessageClose(&umicomThreadMessages, 701U, exchange->producerEndpoint)
        == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "close-producer");
    UmicomThreadExpect(UmicomKernelThreadWake(&umicomThreadScheduler, exchange->consumerThread), "wake-peer-close");
    return exchange->sent;
}
static UmicomU64 UmicomThreadConsumer(void *argument)
{
    UmicomThreadExchange *const exchange = (UmicomThreadExchange *)argument;
    for (;;) {
        UmicomKernelMessage message;
        const UmicomKernelMessageStatus status = UmicomKernelMessageReceive(&umicomThreadMessages,
            702U, exchange->consumerEndpoint, &message, UMICOM_MESSAGE_MAX_BYTES);
        if (status == UMICOM_MESSAGE_PEER_CLOSED) break;
        if (status == UMICOM_MESSAGE_WOULD_BLOCK) {
            ++exchange->waits;
            UmicomThreadExpect(UmicomKernelThreadWait(&umicomThreadScheduler), "consumer-wait");
            continue; /* Always recheck the queue condition after waking. */
        }
        UmicomThreadRequire(status == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "receive-status");
        UmicomThreadRequire(message.sender == 701U && message.bytes == 32U &&
            message.sequence == exchange->received + 1U ? UMICOM_TRUE : UMICOM_FALSE, "packet-metadata");
        for (UmicomSize byte = 0U; byte < 32U; ++byte)
            UmicomThreadRequire(message.data[byte] == (UmicomU8)(exchange->received + byte)
                ? UMICOM_TRUE : UMICOM_FALSE, "copied-payload");
        ++exchange->received;
        const UmicomKernelThreadStatus wake = UmicomKernelThreadWake(&umicomThreadScheduler, exchange->producerThread);
        /* After closing its endpoint, the producer can already be terminal. */
        UmicomThreadRequire(wake == UMICOM_THREAD_OK || wake == UMICOM_THREAD_BAD_STATE
            ? UMICOM_TRUE : UMICOM_FALSE, "wake-producer");
    }
    UmicomThreadRequire(UmicomKernelMessageClose(&umicomThreadMessages, 702U, exchange->consumerEndpoint)
        == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "close-consumer");
    return exchange->received;
}

void UmicomKernelThreadsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("kernel-threads-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomThreadRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "physical-baseline");
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 timerCompare = UmicomPlatformTimerCompareRead(0U);
    UmicomThreadExpect(UmicomKernelSchedulerInitialize(&umicomThreadScheduler), "initialise");

    UmicomThreadCase("round-robin-registers-and-nested-stacks");
    UmicomThreadWork work[3];
    UmicomKernelThreadHandle handles[UMICOM_THREAD_LIMIT];
    for (UmicomSize index = 0U; index < 3U; ++index) {
        work[index].identity = index + 1U;
        work[index].progress = 0U;
        work[index].observedLocal = 0U;
        work[index].deadline = 0U;
        UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler,
            UmicomThreadRoundRobin, &work[index], &handles[index]), "create-round-robin");
    }
    UmicomThreadDrain();
    UmicomThreadRequire(umicomThreadTraceCount == 12U ? UMICOM_TRUE : UMICOM_FALSE, "trace-length");
    for (UmicomSize index = 0U; index < umicomThreadTraceCount; ++index)
        UmicomThreadRequire(umicomThreadTrace[index] == index % 3U + 1U ? UMICOM_TRUE : UMICOM_FALSE, "round-robin-order");
    for (UmicomSize index = 0U; index < 3U; ++index) {
        const UmicomKernelThreadInfo info = UmicomThreadCollect(handles[index]);
        UmicomThreadRequire(info.state == UMICOM_THREAD_FINISHED && info.exitValue == index + 1U &&
            info.dispatches == 6U && info.yields == 5U ? UMICOM_TRUE : UMICOM_FALSE, "thread-continuation-count");
    }
    UmicomThreadRequire(work[0].observedLocal != work[1].observedLocal &&
        work[1].observedLocal != work[2].observedLocal && work[0].observedLocal != work[2].observedLocal
        ? UMICOM_TRUE : UMICOM_FALSE, "distinct-live-stacks");
    UmicomKernelConsoleWriteLine("threads.registers-and-locals=preserved");

    UmicomThreadCase("timer-observation-and-sleep-resumption");
    const UmicomU64 now = UmicomPlatformTimerRead();
    UmicomThreadRequire(now <= ~(UmicomU64)0U - 50000U ? UMICOM_TRUE : UMICOM_FALSE, "deadline-overflow");
    UmicomThreadExpect(UmicomKernelSchedulerAdvanceTime(&umicomThreadScheduler, now), "advance-clock");
    work[0].deadline = now + 50000U;
    work[0].progress = 0U;
    UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadSleeper, &work[0], &handles[0]), "create-sleeper");
    UmicomThreadExpect(UmicomKernelSchedulerRunOne(&umicomThreadScheduler), "enter-sleep");
    UmicomThreadRequire(UmicomKernelSchedulerRunOne(&umicomThreadScheduler) == UMICOM_THREAD_IDLE
        ? UMICOM_TRUE : UMICOM_FALSE, "sleep-is-not-ready");
    /* Poll only from the dispatcher. Time progression does not consume the
     * existing interrupt handler or reprogram the machine-timer compare value. */
    for (UmicomSize poll = 0U; poll < 1000000U && work[0].progress != 2U; ++poll) {
        UmicomThreadExpect(UmicomKernelSchedulerAdvanceTime(&umicomThreadScheduler, UmicomPlatformTimerRead()), "observe-machine-time");
        const UmicomKernelThreadStatus status = UmicomKernelSchedulerRunOne(&umicomThreadScheduler);
        UmicomThreadRequire(status == UMICOM_THREAD_OK || status == UMICOM_THREAD_IDLE ? UMICOM_TRUE : UMICOM_FALSE, "wake-due-sleeper");
    }
    UmicomThreadRequire(work[0].progress == 2U ? UMICOM_TRUE : UMICOM_FALSE, "sleep-resumed");
    (void)UmicomThreadCollect(handles[0]);

    UmicomThreadCase("explicit-wake-and-stale-handle-refusal");
    work[0].progress = 0U;
    UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadWaiter, &work[0], &handles[0]), "create-waiter");
    UmicomThreadDrain();
    UmicomThreadRequire(work[0].progress == 1U ? UMICOM_TRUE : UMICOM_FALSE, "wait-preserves-continuation");
    UmicomThreadExpect(UmicomKernelThreadWake(&umicomThreadScheduler, handles[0]), "explicit-wake");
    UmicomThreadDrain();
    UmicomThreadRequire(work[0].progress == 2U ? UMICOM_TRUE : UMICOM_FALSE, "wait-returned");
    (void)UmicomThreadCollect(handles[0]);
    UmicomKernelThreadInfo ignored;
    UmicomThreadRequire(UmicomKernelThreadQuery(&umicomThreadScheduler, handles[0], &ignored) == UMICOM_THREAD_INVALID_HANDLE
        ? UMICOM_TRUE : UMICOM_FALSE, "reaped-token-refused");

    UmicomThreadCase("capacity-cancellation-and-explicit-reaping");
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index)
        UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadWaiter, &work[0], &handles[index]), "fill-capacity");
    UmicomKernelThreadHandle unchanged = 123U;
    UmicomThreadRequire(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadWaiter, &work[0], &unchanged)
        == UMICOM_THREAD_CAPACITY && unchanged == 123U ? UMICOM_TRUE : UMICOM_FALSE, "capacity-refused-without-output");
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index) {
        UmicomThreadExpect(UmicomKernelThreadCancel(&umicomThreadScheduler, handles[index]), "cancel-ready");
        UmicomThreadRequire(UmicomThreadCollect(handles[index]).state == UMICOM_THREAD_CANCELLED ? UMICOM_TRUE : UMICOM_FALSE, "collect-cancelled");
    }

    UmicomThreadCase("bounded-dispatch-of-cooperative-loop");
    UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadYieldForever, &work[0], &handles[0]), "create-yielding-loop");
    for (UmicomSize dispatch = 0U; dispatch < 8U; ++dispatch)
        UmicomThreadExpect(UmicomKernelSchedulerRunOne(&umicomThreadScheduler), "bounded-dispatch");
    UmicomThreadExpect(UmicomKernelThreadCancel(&umicomThreadScheduler, handles[0]), "cancel-suspended-loop");
    UmicomThreadRequire(UmicomThreadCollect(handles[0]).dispatches == 8U ? UMICOM_TRUE : UMICOM_FALSE, "exact-dispatch-budget");

    UmicomThreadCase("copied-channel-producer-consumer-waiting");
    UmicomThreadExchange exchange;
    /* Initialise every field before any callback receives this borrowed owner. */
    exchange.producerThread = 0U; exchange.consumerThread = 0U;
    exchange.producerEndpoint = 0U; exchange.consumerEndpoint = 0U;
    exchange.sent = 0U; exchange.received = 0U; exchange.waits = 0U;
    UmicomThreadRequire(UmicomKernelMessageInitialize(&umicomThreadMessages) == UMICOM_MESSAGE_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "message-domain");
    UmicomThreadRequire(UmicomKernelMessagePairCreate(&umicomThreadMessages,
        701U, UMICOM_MESSAGE_RIGHT_SEND, 702U, UMICOM_MESSAGE_RIGHT_RECEIVE,
        &exchange.producerEndpoint, &exchange.consumerEndpoint) == UMICOM_MESSAGE_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "message-pair");
    UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadConsumer, &exchange,
        &exchange.consumerThread), "create-consumer");
    UmicomThreadExpect(UmicomKernelThreadCreate(&umicomThreadScheduler, UmicomThreadProducer, &exchange,
        &exchange.producerThread), "create-producer");
    UmicomThreadDrain();
    UmicomThreadRequire(exchange.sent == 24U && exchange.received == 24U && exchange.waits != 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "cooperative-message-delivery");
    UmicomThreadRequire(UmicomThreadCollect(exchange.producerThread).exitValue == 24U &&
        UmicomThreadCollect(exchange.consumerThread).exitValue == 24U ? UMICOM_TRUE : UMICOM_FALSE, "peer-results");
    UmicomKernelMessageSnapshot messageSnapshot;
    UmicomThreadRequire(UmicomKernelMessageSnapshotRead(&umicomThreadMessages, &messageSnapshot) == UMICOM_MESSAGE_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "message-accounting");
    UmicomThreadRequire(messageSnapshot.channels == 0U && messageSnapshot.handles == 0U && messageSnapshot.messages == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "message-ownership-returned");
    UmicomThreadNumber("threads.messages-received", exchange.received);
    UmicomKernelConsoleWriteLine("threads.message-wait-resume=pass");

    UmicomKernelSchedulerInfo final;
    UmicomThreadExpect(UmicomKernelSchedulerSnapshot(&umicomThreadScheduler, &final), "scheduler-final-invariants");
    UmicomThreadRequire(final.freeSlots == UMICOM_THREAD_LIMIT && final.terminal == 0U && final.ready == 0U &&
        final.sleeping == 0U && final.waiting == 0U ? UMICOM_TRUE : UMICOM_FALSE, "all-stacks-reaped");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    /* Compare real fields, not uninitialised structure padding. */
    UmicomThreadRequire(machineBefore.mstatus == machineAfter.mstatus && machineBefore.mie == machineAfter.mie &&
        machineBefore.mtvec == machineAfter.mtvec && machineBefore.mscratch == machineAfter.mscratch &&
        machineBefore.medeleg == machineAfter.medeleg && machineBefore.mideleg == machineAfter.mideleg &&
        machineBefore.satp == machineAfter.satp && machineBefore.pmpcfg0 == machineAfter.pmpcfg0 &&
        machineBefore.pmpaddr0 == machineAfter.pmpaddr0 && machineBefore.mepc == machineAfter.mepc &&
        machineBefore.mcause == machineAfter.mcause && machineBefore.mtval == machineAfter.mtval &&
        timerCompare == UmicomPlatformTimerCompareRead(0U) ? UMICOM_TRUE : UMICOM_FALSE, "machine-control-state-unchanged");
    UmicomRiscvTrapSnapshot trapBefore, trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomThreadRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U && trapAfter.lastCauseCode == 11U
        ? UMICOM_TRUE : UMICOM_FALSE, "original-trap-handler");
    UmicomThreadRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.reservedFrames == after.reservedFrames &&
        before.freeFrames == after.freeFrames ? UMICOM_TRUE : UMICOM_FALSE, "physical-accounting-unchanged");
    UmicomKernelConsoleWriteLine("threads.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("threads.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("threads.frame-accounting=unchanged");
    UmicomThreadNumber("threads.completed-cases", umicomThreadCases);
    UmicomThreadNumber("threads.completed-checks", umicomThreadChecks);
    UmicomKernelConsoleWriteLine("kernel-threads-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_COOPERATIVE_THREADS_READY");
}
