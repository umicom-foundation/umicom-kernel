/* Umicom Kernel cooperative scheduler native tests. Each CTest starts a fresh
 * process, giving every owner genuine zero-filled storage rather than resetting
 * a live token domain. The host adapter performs real stack switches; machine
 * CSR policy is injected, not emulated. Sammy Hegab, Umicom Foundation. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/threads.h"
#include "umicom/kernel/message_channel.h"
extern UmicomBoolean umicomHostMachineReady;
static UmicomKernelScheduler scheduler;
static UmicomKernelScheduler second;
static UmicomKernelMessageDomain messages;
static unsigned int checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "check %u failed at line %d: %s\n", checks, __LINE__, #condition); exit(1); } } while (0)
#define OK(expression) CHECK((expression) == UMICOM_THREAD_OK)

typedef struct Work {
    UmicomKernelScheduler *owner;
    UmicomKernelThreadHandle target;
    UmicomU64 value;
    UmicomU64 deadline;
    UmicomU64 progress;
    unsigned int mode;
} Work;
static UmicomU64 trace[128];
static UmicomSize traceCount;

static UmicomU64 Nested(Work *work, unsigned int depth)
{
    volatile UmicomU64 local[8];
    for (unsigned int index = 0U; index < 8U; ++index) local[index] = work->value + depth + index;
    if (depth != 0U) CHECK(Nested(work, depth - 1U) == work->value);
    else for (unsigned int loop = 0U; loop < 4U; ++loop) {
        CHECK(traceCount < 128U); trace[traceCount++] = work->value;
        ++work->progress;
        OK(UmicomKernelThreadYield(work->owner));
    }
    for (unsigned int index = 0U; index < 8U; ++index)
        CHECK(local[index] == work->value + depth + index);
    return work->value;
}
static UmicomU64 Body(void *argument)
{
    Work *const work = (Work *)argument;
    ++work->progress;
    if (work->mode == 1U) {
        OK(UmicomKernelThreadWait(work->owner));
    } else if (work->mode == 2U) {
        OK(UmicomKernelThreadSleepUntil(work->owner, work->deadline));
    } else if (work->mode == 3U) {
        return Nested(work, 3U);
    } else if (work->mode == 4U) {
        UmicomKernelThreadInfo info = {0};
        OK(UmicomKernelThreadQuery(work->owner, work->target, &info));
        CHECK(info.state == UMICOM_THREAD_RUNNING);
        CHECK(UmicomKernelSchedulerRunOne(work->owner) == UMICOM_THREAD_BAD_STATE);
        CHECK(UmicomKernelSchedulerRunOne(&second) == UMICOM_THREAD_BAD_STATE);
        CHECK(UmicomKernelThreadCancel(work->owner, work->target) == UMICOM_THREAD_BAD_STATE);
        CHECK(UmicomKernelThreadReap(work->owner, work->target, &info) == UMICOM_THREAD_BAD_STATE);
        CHECK(UmicomKernelThreadYield(&second) == UMICOM_THREAD_WRONG_CONTEXT);
    } else if (work->mode == 5U) {
        CHECK(UmicomKernelThreadYield(work->owner) == UMICOM_THREAD_COUNTER_EXHAUSTED);
    } else if (work->mode == 6U) {
        for (;;) { ++work->progress; OK(UmicomKernelThreadYield(work->owner)); }
    }
    ++work->progress;
    return work->value;
}
static UmicomKernelThreadHandle Create(Work *work)
{
    UmicomKernelThreadHandle handle = 0U;
    work->owner = &scheduler;
    OK(UmicomKernelThreadCreate(&scheduler, Body, work, &handle));
    CHECK(handle != 0U); work->target = handle;
    return handle;
}
static UmicomKernelThreadInfo Query(UmicomKernelThreadHandle handle)
{
    UmicomKernelThreadInfo info = {0};
    OK(UmicomKernelThreadQuery(&scheduler, handle, &info)); return info;
}
static UmicomKernelThreadInfo Reap(UmicomKernelThreadHandle handle)
{
    UmicomKernelThreadInfo info = {0};
    OK(UmicomKernelThreadReap(&scheduler, handle, &info)); return info;
}
static void Drain(void)
{
    for (unsigned int count = 0U; count < 100U; ++count) {
        const UmicomKernelThreadStatus status = UmicomKernelSchedulerRunOne(&scheduler);
        if (status == UMICOM_THREAD_IDLE) return;
        CHECK(status == UMICOM_THREAD_OK);
    }
    CHECK(0); /* A test must not disguise a non-terminating callback set. */
}

typedef struct Exchange {
    UmicomKernelThreadHandle senderThread, receiverThread;
    UmicomKernelMessageHandle sender, receiver;
    UmicomSize sent, received, blocked;
} Exchange;
static UmicomU64 Producer(void *argument)
{
    Exchange *const exchange = (Exchange *)argument;
    UmicomU8 payload[32];
    for (UmicomSize packet = 0U; packet < 24U; ++packet) {
        for (UmicomSize byte = 0U; byte < sizeof(payload); ++byte)
            payload[byte] = (UmicomU8)(packet + byte);
        for (;;) {
            const UmicomKernelMessageStatus status = UmicomKernelMessageSend(
                &messages, 101U, exchange->sender, payload, sizeof(payload));
            if (status == UMICOM_MESSAGE_OK) break;
            CHECK(status == UMICOM_MESSAGE_WOULD_BLOCK); ++exchange->blocked;
            /* No yield occurs between observing full and recording WAITING. */
            OK(UmicomKernelThreadWait(&scheduler));
        }
        ++exchange->sent;
        memset(payload, 0xee, sizeof(payload)); /* Queue owns its own copy. */
        OK(UmicomKernelThreadWake(&scheduler, exchange->receiverThread));
    }
    CHECK(UmicomKernelMessageClose(&messages, 101U, exchange->sender) == UMICOM_MESSAGE_OK);
    OK(UmicomKernelThreadWake(&scheduler, exchange->receiverThread));
    return 24U;
}
static UmicomU64 Consumer(void *argument)
{
    Exchange *const exchange = (Exchange *)argument;
    for (;;) {
        UmicomKernelMessage message = {0};
        const UmicomKernelMessageStatus status = UmicomKernelMessageReceive(
            &messages, 102U, exchange->receiver, &message, UMICOM_MESSAGE_MAX_BYTES);
        if (status == UMICOM_MESSAGE_PEER_CLOSED) break;
        if (status == UMICOM_MESSAGE_WOULD_BLOCK) {
            ++exchange->blocked; OK(UmicomKernelThreadWait(&scheduler)); continue;
        }
        CHECK(status == UMICOM_MESSAGE_OK);
        CHECK(message.sender == 101U && message.bytes == 32U && message.sequence == exchange->received + 1U);
        for (UmicomSize byte = 0U; byte < 32U; ++byte)
            CHECK(message.data[byte] == (UmicomU8)(exchange->received + byte));
        ++exchange->received;
        const UmicomKernelThreadStatus wake = UmicomKernelThreadWake(&scheduler, exchange->senderThread);
        CHECK(wake == UMICOM_THREAD_OK || wake == UMICOM_THREAD_BAD_STATE); /* Sender may have finished. */
    }
    CHECK(UmicomKernelMessageClose(&messages, 102U, exchange->receiver) == UMICOM_MESSAGE_OK);
    return exchange->received;
}
static void ExchangeTest(void)
{
    Exchange exchange = {0};
    CHECK(UmicomKernelMessageInitialize(&messages) == UMICOM_MESSAGE_OK);
    CHECK(UmicomKernelMessagePairCreate(&messages,
        101U, UMICOM_MESSAGE_RIGHT_SEND, 102U, UMICOM_MESSAGE_RIGHT_RECEIVE,
        &exchange.sender, &exchange.receiver) == UMICOM_MESSAGE_OK);
    /* Consumer runs first and must wait for a producer that has not run yet. */
    OK(UmicomKernelThreadCreate(&scheduler, Consumer, &exchange, &exchange.receiverThread));
    OK(UmicomKernelThreadCreate(&scheduler, Producer, &exchange, &exchange.senderThread));
    Drain();
    CHECK(exchange.sent == 24U && exchange.received == 24U && exchange.blocked > 0U);
    CHECK(Reap(exchange.senderThread).exitValue == 24U);
    CHECK(Reap(exchange.receiverThread).exitValue == 24U);
    UmicomKernelMessageSnapshot info = {0};
    CHECK(UmicomKernelMessageSnapshotRead(&messages, &info) == UMICOM_MESSAGE_OK);
    CHECK(info.messages == 0U && info.channels == 0U && info.handles == 0U);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *const test = argv[1];
    Work work = {0}; work.value = 77U;
    UmicomKernelThreadHandle handle = 0U;
    UmicomKernelThreadInfo info = {0};
    UmicomKernelSchedulerInfo snapshot = {0};
#define IS(name) (strcmp(test, name) == 0)
    if (IS("null-initialise")) { CHECK(UmicomKernelSchedulerInitialize(NULL) == UMICOM_THREAD_INVALID_ARGUMENT); }
    else if (IS("uninitialised")) { CHECK(UmicomKernelSchedulerRunOne(&scheduler) == UMICOM_THREAD_NOT_INITIALISED); }
    else if (IS("dirty-domain")) {
        scheduler.self = &second; CHECK(UmicomKernelSchedulerInitialize(&scheduler) == UMICOM_THREAD_BAD_STATE);
    } else {
        OK(UmicomKernelSchedulerInitialize(&scheduler));
        OK(UmicomKernelSchedulerInitialize(&second));
        if (IS("reinitialise")) CHECK(UmicomKernelSchedulerInitialize(&scheduler) == UMICOM_THREAD_BAD_STATE);
        else if (IS("copied-domain")) {
            second = scheduler; CHECK(UmicomKernelSchedulerValidate(&second) == UMICOM_THREAD_CORRUPT_STATE);
        } else if (IS("null-entry")) CHECK(UmicomKernelThreadCreate(&scheduler, NULL, &work, &handle) == UMICOM_THREAD_INVALID_ARGUMENT);
        else if (IS("null-output")) CHECK(UmicomKernelThreadCreate(&scheduler, Body, &work, NULL) == UMICOM_THREAD_INVALID_ARGUMENT);
        else if (IS("admission-machine")) {
            umicomHostMachineReady = UMICOM_FALSE;
            CHECK(UmicomKernelThreadCreate(&scheduler, Body, &work, &handle) == UMICOM_THREAD_UNSAFE_MACHINE);
            CHECK(UmicomKernelSchedulerRunOne(&scheduler) == UMICOM_THREAD_UNSAFE_MACHINE);
        } else if (IS("full-capacity") || IS("output-unchanged")) {
            for (unsigned int index = 0U; index < UMICOM_THREAD_LIMIT; ++index) (void)Create(&work);
            handle = 0xdeadbeefU;
            CHECK(UmicomKernelThreadCreate(&scheduler, Body, &work, &handle) == UMICOM_THREAD_CAPACITY);
            CHECK(handle == 0xdeadbeefU);
        } else if (IS("round-robin") || IS("nested-locals")) {
            Work peers[3] = {{0},{0},{0}}; UmicomKernelThreadHandle handles[3] = {0};
            for (unsigned int index = 0U; index < 3U; ++index) {
                peers[index].mode = 3U; peers[index].value = index + 1U; handles[index] = Create(&peers[index]);
            }
            Drain(); CHECK(traceCount == 12U);
            for (UmicomSize index = 0U; index < traceCount; ++index) CHECK(trace[index] == index % 3U + 1U);
            for (unsigned int index = 0U; index < 3U; ++index) {
                info = Reap(handles[index]); CHECK(info.exitValue == index + 1U && info.yields == 4U && info.dispatches == 5U);
            }
        } else if (IS("current-query") || IS("nested-dispatch") || IS("peer-domain-dispatch") || IS("cancel-current") || IS("reap-current")) {
            work.mode = 4U; handle = Create(&work); Drain(); CHECK(Reap(handle).exitValue == 77U);
        } else if (IS("yield-outside")) CHECK(UmicomKernelThreadYield(&scheduler) == UMICOM_THREAD_WRONG_CONTEXT);
        else if (IS("wait-outside")) CHECK(UmicomKernelThreadWait(&scheduler) == UMICOM_THREAD_WRONG_CONTEXT);
        else if (IS("sleep-outside")) CHECK(UmicomKernelThreadSleepUntil(&scheduler, 1U) == UMICOM_THREAD_WRONG_CONTEXT);
        else if (IS("past-deadline")) {
            OK(UmicomKernelSchedulerAdvanceTime(&scheduler, 20U)); work.mode=2U; work.deadline=19U;
            handle=Create(&work); OK(UmicomKernelSchedulerRunOne(&scheduler)); CHECK(Reap(handle).yields == 0U);
        } else if (IS("sleep-deadline") || IS("next-deadline") || IS("equal-deadlines")) {
            Work peer={0}; work.mode=2U; work.deadline=20U;
            handle=Create(&work); peer.mode=2U; peer.deadline=20U; UmicomKernelThreadHandle other=Create(&peer);
            Drain(); CHECK(Query(handle).state == UMICOM_THREAD_SLEEPING);
            OK(UmicomKernelSchedulerSnapshot(&scheduler,&snapshot)); CHECK(snapshot.hasDeadline && snapshot.nextDeadline==20U);
            OK(UmicomKernelSchedulerAdvanceTime(&scheduler,19U)); CHECK(UmicomKernelSchedulerRunOne(&scheduler)==UMICOM_THREAD_IDLE);
            OK(UmicomKernelSchedulerAdvanceTime(&scheduler,20U)); Drain();
            CHECK(Reap(handle).state==UMICOM_THREAD_FINISHED); CHECK(Reap(other).state==UMICOM_THREAD_FINISHED);
        } else if (IS("backwards-clock")) {
            OK(UmicomKernelSchedulerAdvanceTime(&scheduler,10U));
            CHECK(UmicomKernelSchedulerAdvanceTime(&scheduler,9U)==UMICOM_THREAD_CLOCK_REVERSED); CHECK(scheduler.now==10U);
        } else if (IS("maximum-clock")) {
            work.mode=2U; work.deadline=~(UmicomU64)0U; handle=Create(&work); Drain();
            OK(UmicomKernelSchedulerAdvanceTime(&scheduler,~(UmicomU64)0U)); Drain();
            CHECK(Reap(handle).state==UMICOM_THREAD_FINISHED);
        } else if (IS("wake-waiter") || IS("wake-sleeper")) {
            work.mode=IS("wake-waiter")?1U:2U; work.deadline=20U; handle=Create(&work); Drain();
            CHECK(work.progress==1U); OK(UmicomKernelThreadWake(&scheduler,handle)); Drain(); CHECK(work.progress==2U); (void)Reap(handle);
        } else if (IS("wake-ready")) { handle=Create(&work); OK(UmicomKernelThreadWake(&scheduler,handle)); CHECK(work.progress==0U); }
        else if (IS("wake-terminal") || IS("cancel-terminal")) {
            handle=Create(&work); Drain();
            CHECK(UmicomKernelThreadWake(&scheduler,handle)==UMICOM_THREAD_BAD_STATE);
            CHECK(UmicomKernelThreadCancel(&scheduler,handle)==UMICOM_THREAD_BAD_STATE);
        } else if (IS("cancel-ready") || IS("cancel-sleeping") || IS("cancel-waiting")) {
            work.mode=IS("cancel-sleeping")?2U:1U; work.deadline=20U; handle=Create(&work);
            if (!IS("cancel-ready")) Drain();
            OK(UmicomKernelThreadCancel(&scheduler,handle)); CHECK(UmicomKernelSchedulerRunOne(&scheduler)==UMICOM_THREAD_IDLE);
            CHECK(Reap(handle).state==UMICOM_THREAD_CANCELLED);
        } else if (IS("reap-live")) {
            handle=Create(&work); CHECK(UmicomKernelThreadReap(&scheduler,handle,&info)==UMICOM_THREAD_NOT_FINISHED);
        } else if (IS("terminal-value") || IS("scrub-stack")) {
            handle=Create(&work); Drain(); CHECK(Reap(handle).exitValue==77U);
            for (UmicomSize byte=0U;byte<UMICOM_THREAD_STACK_BYTES;++byte) CHECK(scheduler.threads[0].stack[byte]==0U);
        } else if (IS("stale-handle") || IS("generation-reuse")) {
            handle=Create(&work); Drain(); (void)Reap(handle);
            const UmicomKernelThreadHandle fresh=Create(&work); CHECK(fresh!=handle);
            CHECK(UmicomKernelThreadQuery(&scheduler,handle,&info)==UMICOM_THREAD_INVALID_HANDLE);
        } else if (IS("generation-retirement")) {
            scheduler.threads[0].generation=~(UmicomU32)0U; handle=Create(&work); Drain(); (void)Reap(handle);
            CHECK(scheduler.threads[0].retired==UMICOM_TRUE); CHECK((UmicomU32)Create(&work)==2U);
        } else if (IS("separate-domains")) {
            handle=Create(&work); CHECK(UmicomKernelThreadQuery(&second,handle,&info)==UMICOM_THREAD_INVALID_HANDLE);
        } else if (IS("stack-low") || IS("stack-high") || IS("saved-stack")) {
            handle=Create(&work);
            if (IS("stack-low")) scheduler.threads[0].stack[0]=0U;
            else if (IS("stack-high")) scheduler.threads[0].stack[UMICOM_THREAD_STACK_BYTES-1U]=0U;
            else scheduler.threads[0].context.stackPointer=0U;
            CHECK(UmicomKernelSchedulerRunOne(&scheduler)==UMICOM_THREAD_CORRUPT_STATE);
        } else if (IS("bad-current") || IS("bad-next") || IS("corrupt-state")) {
            handle=Create(&work);
            if (IS("bad-current")) scheduler.current=0U;
            else if (IS("bad-next")) scheduler.next=UMICOM_THREAD_LIMIT;
            else scheduler.threads[0].state=(UmicomKernelThreadState)99U;
            CHECK(UmicomKernelSchedulerValidate(&scheduler)==UMICOM_THREAD_CORRUPT_STATE);
        } else if (IS("dispatch-overflow")) {
            handle=Create(&work); scheduler.dispatches=~(UmicomU64)0U;
            CHECK(UmicomKernelSchedulerRunOne(&scheduler)==UMICOM_THREAD_COUNTER_EXHAUSTED); CHECK(work.progress==0U);
        } else if (IS("yield-overflow")) {
            work.mode=5U; handle=Create(&work); scheduler.threads[0].yields=~(UmicomU64)0U;
            Drain(); CHECK(Reap(handle).exitValue==77U);
        } else if (IS("empty-idle")) CHECK(UmicomKernelSchedulerRunOne(&scheduler)==UMICOM_THREAD_IDLE);
        else if (IS("suspended-idle")) {
            work.mode=1U;handle=Create(&work);Drain(); CHECK(Query(handle).state==UMICOM_THREAD_WAITING);
        } else if (IS("repeated-lifetimes")) {
            UmicomKernelThreadHandle old=0U;
            for (unsigned int round=0U;round<2000U;++round) {
                handle=Create(&work); CHECK(handle!=old); Drain(); CHECK(Reap(handle).exitValue==77U); old=handle;
            }
        } else if (IS("bounded-dispatch")) {
            work.mode=6U; handle=Create(&work);
            for (unsigned int round=0U;round<6U;++round) OK(UmicomKernelSchedulerRunOne(&scheduler));
            CHECK(Query(handle).dispatches==6U); OK(UmicomKernelThreadCancel(&scheduler,handle)); (void)Reap(handle);
        } else if (IS("message-exchange")) ExchangeTest();
        else { fprintf(stderr,"unknown case: %s\n",test); return 2; }
    }
    printf("PASS %s (%u checks)\n",test,checks); return 0;
}
