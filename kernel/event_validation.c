/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/event_validation.c
 *
 * PURPOSE:
 *   Exercise remembered signals, wait-any completion and deadlines on real
 *   cooperative Kernel stacks, then coordinate the existing message queues.
 *
 * EDUCATIONAL OVERVIEW:
 *   These checks distinguish an event from a raw thread wake. A remembered
 *   signal survives until it is claimed; an awarded completion survives Reset;
 *   an unrelated wake does not become a successful event. The message example
 *   uses notifications only as hints and retries the actual queue operation.
 *
 *   No trap handler or hardware timer policy is replaced. The dispatcher
 *   supplies logical time for deterministic deadline tests. Machine registers,
 *   timer compare state and physical-frame counters are checked afterwards.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/events.h"
#include "umicom/kernel/message_channel.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"

/* These owners never move and start in BSS. Event waits borrow this scheduler;
 * their lifetimes are shorter than these static owners. No heap is required. */
static UmicomKernelScheduler umicomEventScheduler;
static UmicomKernelEventDomain umicomEvents;
static UmicomKernelMessageDomain umicomEventMessages;
static UmicomU64 umicomEventChecks;
static UmicomU64 umicomEventCases;

static void UmicomEventRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomEventChecks;
    if (condition != UMICOM_FALSE) return;
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("event-reason=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(181U);
    UmicomPlatformHalt();
}
static void UmicomEventExpect(UmicomKernelEventStatus status, const char *reason)
{
    UmicomEventRequire(status == UMICOM_EVENT_OK ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomEventThreadExpect(UmicomKernelThreadStatus status, const char *reason)
{
    UmicomEventRequire(status == UMICOM_THREAD_OK ? UMICOM_TRUE : UMICOM_FALSE, reason);
}
static void UmicomEventNumber(const char *name, UmicomU64 value)
{
    UmicomKernelConsoleWrite(name);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static void UmicomEventCase(const char *name)
{
    ++umicomEventCases;
    UmicomKernelConsoleWrite("events.case=");
    UmicomKernelConsoleWriteLine(name);
}
static UmicomKernelEventHandle UmicomEventNew(UmicomKernelEventMode mode, UmicomBoolean signalled)
{
    UmicomKernelEventHandle handle = 0U;
    UmicomEventExpect(UmicomKernelEventCreate(&umicomEvents, mode, signalled, &handle), "create-event");
    return handle;
}

/* Arguments stay on the dispatcher stack while callbacks borrow them. Nothing
 * here points into a callback stack that cancellation could abandon. */
typedef struct UmicomEventWork {
    UmicomKernelThreadHandle thread;
    UmicomKernelEventHandle events[2];
    UmicomSize count;
    UmicomBoolean timed;
    UmicomU64 deadline;
    UmicomKernelEventResult result;
    UmicomU64 progress;
} UmicomEventWork;

static UmicomU64 UmicomEventWaiter(void *argument)
{
    UmicomEventWork *const work = (UmicomEventWork *)argument;
    volatile UmicomU64 local[4];
    for (UmicomSize index = 0U; index < 4U; ++index) local[index] = 0x7150U + index;
    ++work->progress;
    UmicomEventExpect(UmicomKernelEventWaitAny(&umicomEvents, work->thread,
        work->events, work->count, work->timed, work->deadline, &work->result), "wait-any");
    /* The scheduler must resume this call, including its previous local data. */
    for (UmicomSize index = 0U; index < 4U; ++index) {
        UmicomEventRequire(local[index] == 0x7150U + index ? UMICOM_TRUE : UMICOM_FALSE, "wait-stack-local");
    }
    ++work->progress;
    return (UmicomU64)work->result.outcome;
}
static void UmicomEventStart(UmicomEventWork *work, UmicomKernelEventHandle event)
{
    /* Initialise the complete borrowed argument before a thread can run. */
    work->thread = 0U; work->events[0] = event; work->events[1] = 0U;
    work->count = 1U; work->timed = UMICOM_FALSE; work->deadline = 0U;
    work->result.outcome = UMICOM_EVENT_PENDING;
    work->result.index = UMICOM_EVENT_NO_SELECTION;
    work->result.event = 0U; work->result.observedTime = 0U; work->progress = 0U;
    UmicomEventThreadExpect(UmicomKernelThreadCreate(&umicomEventScheduler,
        UmicomEventWaiter, work, &work->thread), "create-waiter");
}
static void UmicomEventRunReady(void)
{
    /* A broken wake protocol must produce a bounded test failure, not silently
     * turn an unfinished waiter into a passing acceptance result. */
    for (UmicomSize attempt = 0U; attempt < 256U; ++attempt) {
        UmicomEventExpect(UmicomKernelEventPump(&umicomEvents), "pump-events");
        const UmicomKernelThreadStatus status = UmicomKernelSchedulerRunOne(&umicomEventScheduler);
        if (status == UMICOM_THREAD_IDLE) return;
        UmicomEventThreadExpect(status, "dispatch-thread");
    }
    UmicomEventRequire(UMICOM_FALSE, "dispatch-budget-exhausted");
}
static void UmicomEventCollect(UmicomEventWork *work, UmicomKernelEventOutcome outcome)
{
    UmicomEventRequire(work->progress == 2U && work->result.outcome == outcome
        ? UMICOM_TRUE : UMICOM_FALSE, "completion-outcome");
    UmicomKernelThreadInfo result;
    UmicomEventThreadExpect(UmicomKernelThreadReap(&umicomEventScheduler, work->thread, &result), "reap-waiter");
    UmicomEventRequire(result.exitValue == (UmicomU64)outcome ? UMICOM_TRUE : UMICOM_FALSE, "thread-result");
}

/* Queue condition ownership remains in message_channel.c. Event handles carry
 * no sender identity or permission; they merely notify these trusted callbacks
 * that retrying the rights-checked queue operation may now be worthwhile. */
typedef struct UmicomEventExchange {
    UmicomKernelThreadHandle producer;
    UmicomKernelThreadHandle consumer;
    UmicomKernelMessageHandle send;
    UmicomKernelMessageHandle receive;
    UmicomKernelEventHandle readable;
    UmicomKernelEventHandle writable;
    UmicomU64 sent;
    UmicomU64 received;
    UmicomU64 waits;
} UmicomEventExchange;
static void UmicomEventQueueWait(UmicomKernelThreadHandle thread,
    UmicomKernelEventHandle event, UmicomEventExchange *exchange)
{
    UmicomKernelEventResult result;
    UmicomEventExpect(UmicomKernelEventWaitAny(&umicomEvents, thread, &event, 1U,
        UMICOM_FALSE, 0U, &result), "queue-event-wait");
    UmicomEventRequire(result.outcome == UMICOM_EVENT_SIGNALED ? UMICOM_TRUE : UMICOM_FALSE, "queue-event-result");
    ++exchange->waits;
}
static UmicomU64 UmicomEventProducer(void *argument)
{
    UmicomEventExchange *const exchange = (UmicomEventExchange *)argument;
    for (UmicomU64 packet = 0U; packet < 32U; ++packet) {
        UmicomU8 payload[16];
        for (UmicomSize byte = 0U; byte < 16U; ++byte) payload[byte] = (UmicomU8)(packet + byte);
        for (;;) {
            const UmicomKernelMessageStatus status = UmicomKernelMessageSend(
                &umicomEventMessages, 901U, exchange->send, payload, sizeof(payload));
            if (status == UMICOM_MESSAGE_OK) break;
            UmicomEventRequire(status == UMICOM_MESSAGE_WOULD_BLOCK ? UMICOM_TRUE : UMICOM_FALSE, "send-status");
            UmicomEventQueueWait(exchange->producer, exchange->writable, exchange);
            /* A wake is not a reserved queue slot. Retry the actual send. */
        }
        ++exchange->sent;
        for (UmicomSize byte = 0U; byte < 16U; ++byte) payload[byte] = 0xeeU;
        UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, exchange->readable), "notify-readable");
    }
    UmicomEventRequire(UmicomKernelMessageClose(&umicomEventMessages, 901U, exchange->send)
        == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "close-producer");
    /* Peer closure is another reason to retry RECEIVE, even with no new data. */
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, exchange->readable), "notify-peer-close");
    return exchange->sent;
}
static UmicomU64 UmicomEventConsumer(void *argument)
{
    UmicomEventExchange *const exchange = (UmicomEventExchange *)argument;
    for (;;) {
        UmicomKernelMessage message;
        const UmicomKernelMessageStatus status = UmicomKernelMessageReceive(
            &umicomEventMessages, 902U, exchange->receive, &message, UMICOM_MESSAGE_MAX_BYTES);
        if (status == UMICOM_MESSAGE_PEER_CLOSED) break;
        if (status == UMICOM_MESSAGE_WOULD_BLOCK) {
            UmicomEventQueueWait(exchange->consumer, exchange->readable, exchange);
            continue;
        }
        UmicomEventRequire(status == UMICOM_MESSAGE_OK && message.bytes == 16U &&
            message.sender == 901U && message.sequence == exchange->received + 1U
            ? UMICOM_TRUE : UMICOM_FALSE, "received-metadata");
        for (UmicomSize byte = 0U; byte < 16U; ++byte) {
            UmicomEventRequire(message.data[byte] == (UmicomU8)(exchange->received + byte)
                ? UMICOM_TRUE : UMICOM_FALSE, "copied-message-content");
        }
        ++exchange->received;
        UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, exchange->writable), "notify-writable");
    }
    UmicomEventRequire(UmicomKernelMessageClose(&umicomEventMessages, 902U, exchange->receive)
        == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "close-consumer");
    return exchange->received;
}

void UmicomKernelEventsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("kernel-events-test=begin");
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 timerCompare = UmicomPlatformTimerCompareRead(0U);
    UmicomKernelPhysicalMemorySnapshot framesBefore, framesAfter;
    UmicomEventRequire(UmicomKernelPhysicalMemorySnapshotRead(&framesBefore) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "baseline-frame-accounting");
    UmicomEventThreadExpect(UmicomKernelSchedulerInitialize(&umicomEventScheduler), "scheduler-initialise");
    UmicomEventExpect(UmicomKernelEventDomainInitialize(&umicomEvents, &umicomEventScheduler), "events-initialise");
    UmicomEventWork first, second, third;

    UmicomEventCase("signal-before-wait");
    UmicomKernelEventHandle event = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "remember-signal");
    UmicomEventStart(&first, event); UmicomEventRunReady();
    UmicomEventCollect(&first, UMICOM_EVENT_SIGNALED);
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-latch");

    UmicomEventCase("manual-broadcast-survives-reset");
    event = UmicomEventNew(UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE);
    UmicomEventStart(&first, event); UmicomEventStart(&second, event); UmicomEventRunReady();
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "broadcast");
    UmicomEventExpect(UmicomKernelEventReset(&umicomEvents, event), "reset-after-award");
    UmicomEventRunReady();
    UmicomEventCollect(&first, UMICOM_EVENT_SIGNALED); UmicomEventCollect(&second, UMICOM_EVENT_SIGNALED);
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-broadcast");

    UmicomEventCase("auto-reset-awards-one-waiter");
    event = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    /* Register each waiter before creating the next. FIFO means registration
     * order, not creation order: the round-robin cursor survives earlier cases. */
    UmicomEventStart(&first, event); UmicomEventRunReady();
    UmicomEventStart(&second, event); UmicomEventRunReady();
    UmicomEventStart(&third, event); UmicomEventRunReady();
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "award-first");
    UmicomEventRunReady();
    UmicomEventRequire(first.progress == 2U && second.progress == 1U && third.progress == 1U
        ? UMICOM_TRUE : UMICOM_FALSE, "oldest-waiter-first");
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "award-second");
    UmicomEventRunReady();
    UmicomEventRequire(second.progress == 2U && third.progress == 1U ? UMICOM_TRUE : UMICOM_FALSE, "one-credit-only");
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-third-wait");
    UmicomEventRunReady();
    UmicomEventCollect(&first, UMICOM_EVENT_SIGNALED);
    UmicomEventCollect(&second, UMICOM_EVENT_SIGNALED);
    UmicomEventCollect(&third, UMICOM_EVENT_CLOSED);

    UmicomEventCase("wait-any-close-and-stale-handle");
    event = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    const UmicomKernelEventHandle other = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventStart(&first, event); first.events[1] = other; first.count = 2U;
    UmicomEventRunReady();
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, other), "close-selected-event");
    const UmicomKernelEventHandle replacement = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventRequire(replacement != other &&
        UmicomKernelEventSignal(&umicomEvents, other) == UMICOM_EVENT_INVALID_HANDLE
        ? UMICOM_TRUE : UMICOM_FALSE, "stale-event-refused");
    UmicomEventRunReady();
    UmicomEventRequire(first.result.index == 1U && first.result.event == other ? UMICOM_TRUE : UMICOM_FALSE, "closed-selection");
    UmicomEventCollect(&first, UMICOM_EVENT_CLOSED);
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-unselected-event");
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, replacement), "close-replacement");

    UmicomEventCase("deadline-and-spurious-wake");
    event = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventStart(&first, event); first.timed = UMICOM_TRUE; first.deadline = 100U;
    UmicomEventRunReady();
    UmicomEventThreadExpect(UmicomKernelThreadWake(&umicomEventScheduler, first.thread), "raw-wake");
    UmicomEventRunReady();
    UmicomEventRequire(first.progress == 1U ? UMICOM_TRUE : UMICOM_FALSE, "wake-is-not-signal");
    UmicomEventThreadExpect(UmicomKernelSchedulerAdvanceTime(&umicomEventScheduler, 100U), "advance-deadline");
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "signal-after-deadline");
    UmicomEventRunReady();
    UmicomEventCollect(&first, UMICOM_EVENT_TIMED_OUT);
    UmicomEventStart(&second, event); UmicomEventRunReady();
    UmicomEventCollect(&second, UMICOM_EVENT_SIGNALED); /* Late credit was not lost. */
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-deadline-event");

    UmicomEventCase("cancelled-waiter-does-not-steal-signal");
    event = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventStart(&first, event); UmicomEventRunReady();
    UmicomEventThreadExpect(UmicomKernelThreadCancel(&umicomEventScheduler, first.thread), "cancel-waiter");
    UmicomKernelThreadInfo terminal;
    UmicomEventThreadExpect(UmicomKernelThreadReap(&umicomEventScheduler, first.thread, &terminal), "reap-cancelled");
    UmicomEventExpect(UmicomKernelEventSignal(&umicomEvents, event), "signal-after-cancellation");
    UmicomEventStart(&second, event); UmicomEventRunReady();
    UmicomEventCollect(&second, UMICOM_EVENT_SIGNALED);
    UmicomEventRequire(first.progress == 1U ? UMICOM_TRUE : UMICOM_FALSE, "cancelled-stack-not-resumed");
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, event), "close-cancel-event");

    UmicomEventCase("message-condition-events");
    UmicomEventExchange exchange;
    exchange.producer = 0U; exchange.consumer = 0U; exchange.send = 0U; exchange.receive = 0U;
    exchange.sent = 0U; exchange.received = 0U; exchange.waits = 0U;
    exchange.readable = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    exchange.writable = UmicomEventNew(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
    UmicomEventRequire(UmicomKernelMessageInitialize(&umicomEventMessages) == UMICOM_MESSAGE_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "message-domain");
    UmicomEventRequire(UmicomKernelMessagePairCreate(&umicomEventMessages,
        901U, UMICOM_MESSAGE_RIGHT_SEND, 902U, UMICOM_MESSAGE_RIGHT_RECEIVE,
        &exchange.send, &exchange.receive) == UMICOM_MESSAGE_OK ? UMICOM_TRUE : UMICOM_FALSE, "message-pair");
    UmicomEventThreadExpect(UmicomKernelThreadCreate(&umicomEventScheduler,
        UmicomEventConsumer, &exchange, &exchange.consumer), "consumer-create");
    UmicomEventThreadExpect(UmicomKernelThreadCreate(&umicomEventScheduler,
        UmicomEventProducer, &exchange, &exchange.producer), "producer-create");
    UmicomEventRunReady();
    UmicomEventThreadExpect(UmicomKernelThreadReap(&umicomEventScheduler, exchange.producer, &terminal), "producer-reap");
    UmicomEventRequire(terminal.exitValue == 32U ? UMICOM_TRUE : UMICOM_FALSE, "producer-result");
    UmicomEventThreadExpect(UmicomKernelThreadReap(&umicomEventScheduler, exchange.consumer, &terminal), "consumer-reap");
    UmicomEventRequire(terminal.exitValue == 32U && exchange.sent == 32U &&
        exchange.received == 32U && exchange.waits != 0U ? UMICOM_TRUE : UMICOM_FALSE, "exchange-result");
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, exchange.readable), "close-readable");
    UmicomEventExpect(UmicomKernelEventClose(&umicomEvents, exchange.writable), "close-writable");
    UmicomKernelMessageSnapshot messages;
    UmicomEventRequire(UmicomKernelMessageSnapshotRead(&umicomEventMessages, &messages) == UMICOM_MESSAGE_OK &&
        messages.channels == 0U && messages.handles == 0U && messages.messages == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "message-accounting");
    UmicomEventNumber("events.messages-received", exchange.received);
    UmicomKernelConsoleWriteLine("events.message-coordination=pass");

    UmicomKernelEventSnapshot events;
    UmicomEventExpect(UmicomKernelEventSnapshotRead(&umicomEvents, &events), "event-accounting");
    UmicomEventRequire(events.openEvents == 0U && events.signalledEvents == 0U &&
        events.pendingWaits == 0U && events.completedWaits == 0U ? UMICOM_TRUE : UMICOM_FALSE, "all-events-released");
    UmicomKernelSchedulerInfo scheduler;
    UmicomEventThreadExpect(UmicomKernelSchedulerSnapshot(&umicomEventScheduler, &scheduler), "scheduler-accounting");
    UmicomEventRequire(scheduler.freeSlots == UMICOM_THREAD_LIMIT ? UMICOM_TRUE : UMICOM_FALSE, "all-stacks-reaped");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomEventRequire(machineBefore.mstatus == machineAfter.mstatus && machineBefore.mie == machineAfter.mie &&
        machineBefore.mtvec == machineAfter.mtvec && machineBefore.mscratch == machineAfter.mscratch &&
        machineBefore.medeleg == machineAfter.medeleg && machineBefore.mideleg == machineAfter.mideleg &&
        machineBefore.satp == machineAfter.satp && machineBefore.pmpcfg0 == machineAfter.pmpcfg0 &&
        machineBefore.pmpaddr0 == machineAfter.pmpaddr0 && machineBefore.mepc == machineAfter.mepc &&
        machineBefore.mcause == machineAfter.mcause && machineBefore.mtval == machineAfter.mtval &&
        timerCompare == UmicomPlatformTimerCompareRead(0U) ? UMICOM_TRUE : UMICOM_FALSE, "machine-state");
    UmicomRiscvTrapSnapshot trapBefore, trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore); UmicomRiscvTriggerMachineEcall(); UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomEventRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U && trapAfter.lastCauseCode == 11U
        ? UMICOM_TRUE : UMICOM_FALSE, "original-trap-handler");
    UmicomEventRequire(UmicomKernelPhysicalMemorySnapshotRead(&framesAfter) == UMICOM_KERNEL_MEMORY_OK &&
        framesBefore.allocatedFrames == framesAfter.allocatedFrames &&
        framesBefore.reservedFrames == framesAfter.reservedFrames &&
        framesBefore.freeFrames == framesAfter.freeFrames ? UMICOM_TRUE : UMICOM_FALSE, "frame-accounting");
    UmicomKernelConsoleWriteLine("events.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("events.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("events.frame-accounting=unchanged");
    UmicomEventNumber("events.completed-cases", umicomEventCases);
    UmicomEventNumber("events.completed-checks", umicomEventChecks);
    UmicomKernelConsoleWriteLine("kernel-events-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_EVENTS_READY");
}
