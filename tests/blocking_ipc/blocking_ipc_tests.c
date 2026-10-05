/*-----------------------------------------------------------------------------
 * Umicom Kernel blocking IPC native tests
 * File: tests/blocking_ipc/blocking_ipc_tests.c
 *
 * PURPOSE:
 *   Test actual C scheduling, wait ownership, queue and copy policy. Privileged
 *   entry and timer registers remain explicit host models, not guest evidence.
 *
 * EDUCATIONAL NOTE:
 *   Reuse the existing scheduler test's RAM, ELF and CSR model without editing
 *   it. Only its entry function gets a test-local name so this file can add a
 *   chosen message ECALL before delegating the established timer/fault cases.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#define main UmicomBaselineSchedulingMain
#define UmicomRiscvUserExecuteFrame UmicomBaselineSchedulingEntry
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/user_ipc.h"

#define UMICOM_IPC_MODEL_CALL 100U
#define UMICOM_IPC_MODEL_CROSS ((UmicomAddress)0x600ff0U)
static UmicomKernelUserIpc umicomModelIpc;
static UmicomU64 umicomModelIpcNumber;
static UmicomU64 umicomModelIpcArgs[4];

UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    if (umicomModelMode != UMICOM_IPC_MODEL_CALL)
        return UmicomBaselineSchedulingEntry(request, session, frame);
    ++umicomModelEntries;
    UmicomModelRequire(request->entryVirtualAddress == frame->mepc &&
        request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress,
        "model entry uses selected continuation");
    frame->mstatus = (UmicomU64)2U << 32U;
    frame->reserved = 0U; frame->mcause = 8U; frame->mtval = 0U;
    frame->x17_a7 = umicomModelIpcNumber;
    frame->x10_a0 = umicomModelIpcArgs[0]; frame->x11_a1 = umicomModelIpcArgs[1];
    frame->x12_a2 = umicomModelIpcArgs[2]; frame->x13_a3 = umicomModelIpcArgs[3];
    if (UmicomKernelUserTrapDispatch(session, frame) == 0U) return 0U;
    /* A completed call runs until a quantum ends. A blocked call above stops
     * immediately, preserving its ECALL cause and already advanced next PC. */
    umicomModelNow = umicomModelCompare;
    frame->mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
    UmicomModelRequire(UmicomKernelUserTrapDispatch(session, frame) == 0U, "model quantum stops");
    return 0U;
}
static void UmicomIpcModelCall(UmicomKernelUserTaskHandle task, UmicomU64 number,
    UmicomKernelMessageHandle endpoint, UmicomAddress address, UmicomU64 bytes, UmicomU64 timeout)
{
    umicomModelMode = UMICOM_IPC_MODEL_CALL;
    umicomModelIpcNumber = number;
    umicomModelIpcArgs[0] = endpoint; umicomModelIpcArgs[1] = (UmicomU64)address;
    umicomModelIpcArgs[2] = bytes; umicomModelIpcArgs[3] = timeout;
    umicomModelScheduler.next = (UmicomU32)task - 1U;
    UmicomModelRequire(UmicomModelRun() == task, "expected task dispatched");
}
static void UmicomIpcModelBlock(UmicomKernelUserTaskHandle task, UmicomKernelMessageHandle endpoint, UmicomU64 timeout)
{
    UmicomIpcModelCall(task, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, endpoint, UMICOM_IPC_MODEL_CROSS, 280U, timeout);
    UmicomModelRequire(UmicomModelInfo(task).state == UMICOM_USER_TASK_BLOCKED, "receive really blocked");
}
static void UmicomIpcModelPair(UmicomKernelUserTaskHandle a, UmicomKernelUserTaskHandle b,
    UmicomKernelMessageHandle *receive, UmicomKernelMessageHandle *send)
{
    UmicomModelRequire(UmicomKernelUserIpcConnect(&umicomModelIpc, a,
        UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY | UMICOM_MESSAGE_RIGHT_DUPLICATE | UMICOM_MESSAGE_RIGHT_TRANSFER,
        b, UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY, receive, send) == UMICOM_MESSAGE_OK,
        "connect trusted identities");
}
static void UmicomIpcModelSend(UmicomKernelUserTaskHandle sender, UmicomKernelMessageHandle endpoint, UmicomU8 value)
{
    UmicomU8 payload[32]; memset(payload, value, sizeof(payload));
    UmicomModelRequire(UmicomKernelMessageSend(&umicomModelIpc.messages, UmicomModelInfo(sender).identity,
        endpoint, payload, sizeof(payload)) == UMICOM_MESSAGE_OK, "Kernel producer sends");
}
static void UmicomIpcModelStore(UmicomKernelUserTaskHandle task, UmicomU8 value, UmicomSize bytes)
{
    UmicomU8 data[64]; memset(data, value, sizeof(data));
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize count = bytes - offset < sizeof(data) ? bytes - offset : sizeof(data);
        UmicomModelRequire(UmicomKernelUserMemoryWrite(&UmicomModelRecord(task)->process.report.memory,
            UMICOM_IPC_MODEL_CROSS + offset, data, count) == UMICOM_USER_RESULT_OK, "prepare user span");
        offset += count;
    }
}
static UmicomU8 UmicomIpcModelByte(UmicomKernelUserTaskHandle task, UmicomAddress address)
{
    UmicomU8 byte = 0U;
    UmicomModelRequire(UmicomKernelUserMemoryRead(&UmicomModelRecord(task)->process.report.memory,
        address, &byte, 1U) == UMICOM_USER_RESULT_OK, "read owned byte");
    return byte;
}
static void UmicomIpcModelPump(void)
{
    UmicomModelRequire(UmicomKernelUserIpcPump(&umicomModelScheduler) != UMICOM_FALSE, "pump conditions");
    UmicomModelRequire(UmicomKernelUserSchedulerValidate(&umicomModelScheduler) == UMICOM_USER_SCHEDULE_OK,
        "scheduler and pending ownership validate");
}
static void UmicomIpcModelCleanup(void)
{
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        UmicomKernelUserTask *task = &umicomModelScheduler.tasks[slot];
        if (task->state == UMICOM_USER_TASK_EMPTY) continue;
        const UmicomKernelUserTaskHandle token = ((UmicomU64)task->generation << 32U) | (slot + 1U);
        if (task->state == UMICOM_USER_TASK_BLOCKED)
            UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, token) == UMICOM_USER_SCHEDULE_OK,
                "cancel pending operation before reaping");
        UmicomModelCancelReap(token);
    }
    UmicomModelNoLeaks();
    UmicomKernelMessageSnapshot messages;
    UmicomKernelUserIpcInfo waits;
    UmicomModelRequire(UmicomKernelMessageSnapshotRead(&umicomModelIpc.messages, &messages) == UMICOM_MESSAGE_OK &&
        messages.channels == 0U && messages.handles == 0U && messages.messages == 0U, "no message owner leaked");
    UmicomModelRequire(UmicomKernelUserIpcSnapshot(&umicomModelIpc, &waits) != UMICOM_FALSE && waits.waiting == 0U,
        "no pending request leaked");
}
static int UmicomIpcModelCase(const char *name)
{
    UmicomModelSetup();
    /* The real ELF loader supplies two writable pages for boundary tests. */
    UmicomModelPut(64U + 2U * 56U + 40U, 8192U, 8U);
    UmicomModelRequire(UmicomKernelUserIpcAttach(&umicomModelIpc, &umicomModelScheduler) == UMICOM_USER_SCHEDULE_OK,
        "attach before admission");
    if (!strcmp(name, "null-and-repeat-attach")) {
        UmicomModelRequire(UmicomKernelUserIpcAttach(NULL, &umicomModelScheduler) == UMICOM_USER_SCHEDULE_INVALID_ARGUMENT,
            "null attachment refused");
        UmicomModelRequire(UmicomKernelUserIpcAttach(&umicomModelIpc, &umicomModelScheduler) == UMICOM_USER_SCHEDULE_BAD_STATE,
            "reinitialisation refused");
        UmicomModelRequire(UmicomKernelUserIpcPump(NULL) == UMICOM_FALSE, "null pump refused"); return 1;
    }
    if (!strcmp(name, "copied-domain")) {
        UmicomKernelUserIpc *copy = malloc(sizeof(*copy)); UmicomModelRequire(copy != NULL, "host allocation");
        *copy = umicomModelIpc; UmicomKernelUserIpcInfo info;
        UmicomModelRequire(UmicomKernelUserIpcSnapshot(copy, &info) == UMICOM_FALSE, "copied owner refused");
        free(copy); return 1;
    }
    UmicomKernelUserTaskHandle receiver = UmicomModelCreate(64U), sender = UmicomModelCreate(64U);
    UmicomKernelMessageHandle receive = 0U, send = 0U;
    UmicomIpcModelPair(receiver, sender, &receive, &send);
    if (!strcmp(name, "startup-argument")) {
        UmicomModelRequire(UmicomKernelUserTaskSetArgument(&umicomModelScheduler, receiver, receive) == UMICOM_USER_SCHEDULE_OK,
            "fresh argument accepted");
        UmicomIpcModelBlock(receiver, receive, UMICOM_IPC_WAIT_FOREVER);
        UmicomModelRequire(UmicomKernelUserTaskSetArgument(&umicomModelScheduler, receiver, 0U) == UMICOM_USER_SCHEDULE_BAD_STATE,
            "retained result cannot be overwritten"); return 1;
    }
    if (!strcmp(name, "unbound-call")) {
        UmicomRiscvTrapFrame frame = UmicomModelRecord(receiver)->frame;
        frame.mcause = 8U; frame.x17_a7 = UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT;
        UmicomModelRequire(UmicomKernelUserTrapDispatch(&UmicomModelRecord(receiver)->process.report, &frame) == 1U &&
            frame.x10_a0 == UMICOM_MESSAGE_SERVICE_UNBOUND, "unbound context gains no domain"); return 1;
    }
    if (!strcmp(name, "wrong-owner")) {
        UmicomIpcModelCall(sender, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, receive, UMICOM_IPC_MODEL_CROSS, 280U, UMICOM_IPC_WAIT_FOREVER);
        UmicomModelRequire(UmicomModelRecord(sender)->frame.x10_a0 == UMICOM_MESSAGE_WRONG_OWNER && umicomModelIpc.blocked == 0U,
            "another identity's token refused"); return 1;
    }
    if (!strcmp(name, "nonblocking-preserved") || !strcmp(name, "zero-timeout")) {
        UmicomIpcModelCall(receiver, !strcmp(name, "zero-timeout") ? UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT :
            UMICOM_USER_CALL_MESSAGE_RECEIVE, receive, UMICOM_IPC_MODEL_CROSS, 280U, 0U);
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_WOULD_BLOCK && umicomModelIpc.blocked == 0U,
            "poll returns without suspension"); return 1;
    }
    if (!strcmp(name, "bad-buffer") || !strcmp(name, "read-only-buffer") || !strcmp(name, "small-capacity") ||
        !strcmp(name, "oversized-capacity") || !strcmp(name, "oversized-timeout") || !strcmp(name, "rights") ||
        !strcmp(name, "empty-send") || !strcmp(name, "deadline-overflow")) {
        UmicomU64 number = UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, bytes = 280U, timeout = UMICOM_IPC_WAIT_FOREVER;
        UmicomAddress address = ~(UmicomAddress)0U - 7U; UmicomU64 result = UMICOM_MESSAGE_BAD_USER_BUFFER;
        if (!strcmp(name, "read-only-buffer")) address = 0x500000U;
        if (!strcmp(name, "small-capacity")) { bytes = 23U; result = UMICOM_MESSAGE_INVALID_ARGUMENT; }
        if (!strcmp(name, "oversized-capacity")) { bytes = 281U; result = UMICOM_MESSAGE_TOO_LARGE; }
        if (!strcmp(name, "oversized-timeout")) { timeout = UMICOM_IPC_WAIT_MAX_TICKS + 1U; result = UMICOM_MESSAGE_INVALID_ARGUMENT; }
        if (!strcmp(name, "rights")) { number = UMICOM_USER_CALL_MESSAGE_SEND_WAIT; address = UMICOM_IPC_MODEL_CROSS; bytes = 32U; result = UMICOM_MESSAGE_ACCESS_DENIED; }
        if (!strcmp(name, "empty-send")) { number = UMICOM_USER_CALL_MESSAGE_SEND_WAIT; bytes = 0U; result = UMICOM_MESSAGE_INVALID_ARGUMENT; }
        if (!strcmp(name, "deadline-overflow")) {
            /* The hardware adapter correctly rejects a quantum at this time;
             * isolate the inner call's independent overflow check directly. */
            UmicomKernelUserTask *task = UmicomModelRecord(receiver);
            UmicomModelRequire(UmicomKernelUserIpcBegin(&umicomModelScheduler, task) != UMICOM_FALSE, "bind test invocation");
            task->state = UMICOM_USER_TASK_RUNNING;
            UmicomRiscvTrapFrame frame = task->frame;
            frame.mcause = 8U; frame.x17_a7 = number; frame.x13_a3 = 8U;
            umicomModelNow = ~(UmicomU64)0U - 4U;
            UmicomModelRequire(UmicomKernelUserTrapDispatch(&task->process.report, &frame) == 1U &&
                frame.x10_a0 == UMICOM_MESSAGE_INVALID_ARGUMENT, "overflow refused before side effects");
            UmicomModelRequire(UmicomKernelUserIpcEnd(&umicomModelScheduler, task) != UMICOM_FALSE, "unbind");
            task->state = UMICOM_USER_TASK_READY; umicomModelNow = 100U; return 1;
        }
        UmicomIpcModelCall(receiver, number, receive, address, bytes, timeout);
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == result && umicomModelIpc.blocked == 0U,
            "invalid call did not register a wait"); return 1;
    }
    if (!strcmp(name, "ready-at-admission")) {
        UmicomIpcModelSend(sender, send, 0x45U);
        UmicomIpcModelCall(receiver, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, receive, UMICOM_IPC_MODEL_CROSS, 280U, 0U);
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_OK && umicomModelIpc.blocked == 0U,
            "queued packet available without sleeping"); return 1;
    }
    if (!strcmp(name, "send-snapshot") || !strcmp(name, "cancel-send") || !strcmp(name, "send-peer-close") ||
        !strcmp(name, "send-timeout")) {
        for (UmicomU8 i = 0U; i < UMICOM_MESSAGE_QUEUE_DEPTH; ++i) UmicomIpcModelSend(sender, send, i);
        UmicomIpcModelStore(sender, 0x59U, 32U);
        UmicomIpcModelCall(sender, UMICOM_USER_CALL_MESSAGE_SEND_WAIT, send, UMICOM_IPC_MODEL_CROSS, 32U,
            !strcmp(name, "send-timeout") ? 500U : UMICOM_IPC_WAIT_FOREVER);
        UmicomModelRequire(UmicomModelInfo(sender).state == UMICOM_USER_TASK_BLOCKED, "full queue blocks sender");
        if (!strcmp(name, "send-timeout")) {
            umicomModelNow += 500U; UmicomIpcModelPump();
            UmicomModelRequire(UmicomModelRecord(sender)->frame.x10_a0 == UMICOM_IPC_RESULT_TIMED_OUT,
                "pending send timed out without enqueue"); return 1;
        }
        if (!strcmp(name, "send-peer-close")) {
            UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, receiver) == UMICOM_USER_SCHEDULE_OK, "cancel receiver");
            UmicomIpcModelPump(); UmicomModelRequire(UmicomModelRecord(sender)->frame.x10_a0 == UMICOM_MESSAGE_PEER_CLOSED,
                "closed receiver completes waiting sender"); return 1;
        }
        UmicomIpcModelStore(sender, 0xeeU, 32U); /* Deliberate Kernel-side mutation while suspended. */
        if (!strcmp(name, "cancel-send"))
            UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, sender) == UMICOM_USER_SCHEDULE_OK, "cancel fifth packet");
        UmicomKernelMessage message;
        for (UmicomSize i = 0U; i < 4U; ++i) {
            UmicomModelRequire(UmicomKernelMessageReceive(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity,
                receive, &message, 256U) == UMICOM_MESSAGE_OK, "drain accepted packet");
            if (i == 0U) UmicomIpcModelPump();
        }
        const UmicomKernelMessageStatus status = UmicomKernelMessageReceive(&umicomModelIpc.messages,
            UmicomModelInfo(receiver).identity, receive, &message, 256U);
        UmicomModelRequire(!strcmp(name, "send-snapshot") ?
            status == UMICOM_MESSAGE_OK && message.data[0] == 0x59U : status == UMICOM_MESSAGE_PEER_CLOSED,
            "retained copy or cancelled operation has precise lifetime"); return 1;
    }
    if (!strcmp(name, "hardware-refusal") || !strcmp(name, "occupied-binding")) {
        umicomModelMode = UMICOM_MODEL_REFUSE;
        if (!strcmp(name, "occupied-binding"))
            UmicomModelRequire(UmicomKernelMessageServiceBind(&umicomModelIpc.messages, 99U) == UMICOM_MESSAGE_OK, "occupy global service");
        UmicomKernelUserTaskHandle selected = 77U;
        UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &selected)
            == UMICOM_USER_SCHEDULE_ENTRY_REFUSED && selected == 77U && UmicomModelInfo(receiver).slices == 0U,
            "refusal spends no budget and publishes no captured task");
        if (!strcmp(name, "occupied-binding"))
            UmicomModelRequire(UmicomKernelMessageServiceUnbind(&umicomModelIpc.messages, 99U) == UMICOM_MESSAGE_OK, "release original binding");
        else {
            UmicomModelRequire(UmicomKernelMessageServiceBind(&umicomModelIpc.messages, 99U) == UMICOM_MESSAGE_OK, "no binding escaped refused hardware entry");
            UmicomModelRequire(UmicomKernelMessageServiceUnbind(&umicomModelIpc.messages, 99U) == UMICOM_MESSAGE_OK, "release check binding");
        }
        return 1;
    }
    if (!strcmp(name, "call-budget") || !strcmp(name, "slice-budget") || !strcmp(name, "order-exhaustion")) {
        if (!strcmp(name, "call-budget")) UmicomModelRecord(receiver)->process.report.callCount = UMICOM_USER_CALL_LIMIT;
        if (!strcmp(name, "slice-budget")) UmicomModelRecord(receiver)->sliceLimit = 1U;
        if (!strcmp(name, "order-exhaustion")) umicomModelIpc.nextOrder = 0U;
        UmicomIpcModelCall(receiver, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, receive, UMICOM_IPC_MODEL_CROSS, 280U, UMICOM_IPC_WAIT_FOREVER);
        UmicomModelRequire(!strcmp(name, "order-exhaustion") ?
            UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_SEQUENCE_EXHAUSTED :
            UmicomModelInfo(receiver).state == UMICOM_USER_TASK_EXHAUSTED, "bounded admission cannot replenish authority");
        UmicomModelRequire(umicomModelIpc.waits[0].pending == UMICOM_FALSE, "no exhausted wait retained"); return 1;
    }
    if (!strcmp(name, "idle-with-waiters")) {
        UmicomKernelMessageHandle anotherReceive = 0U, anotherSend = 0U;
        UmicomModelRequire(UmicomKernelMessagePairCreate(&umicomModelIpc.messages, UmicomModelInfo(sender).identity,
            UMICOM_MESSAGE_RIGHT_RECEIVE, UmicomModelInfo(receiver).identity, UMICOM_MESSAGE_RIGHT_SEND,
            &anotherReceive, &anotherSend) == UMICOM_MESSAGE_OK, "second private channel");
        UmicomIpcModelBlock(receiver, receive, UMICOM_IPC_WAIT_FOREVER);
        UmicomIpcModelBlock(sender, anotherReceive, UMICOM_IPC_WAIT_FOREVER);
        const unsigned entries = umicomModelEntries; const UmicomU64 dispatches = umicomModelScheduler.dispatches;
        for (unsigned i = 0U; i < 100U; ++i) {
            UmicomKernelUserTaskHandle output = 17U;
            UmicomModelRequire(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &output)
                == UMICOM_USER_SCHEDULE_IDLE && output == 17U, "blocked tasks not selected");
        }
        UmicomModelRequire(entries == umicomModelEntries && dispatches == umicomModelScheduler.dispatches,
            "no user polling or spent quanta"); return 1;
    }
    if (!strcmp(name, "fifo-waiters") || !strcmp(name, "cancel-oldest")) {
        const UmicomKernelUserTaskHandle other = UmicomModelCreate(64U); UmicomKernelMessageHandle shared = 0U;
        UmicomModelRequire(UmicomKernelMessageGrant(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity,
            receive, UmicomModelInfo(other).identity, UMICOM_MESSAGE_RIGHT_RECEIVE, &shared) == UMICOM_MESSAGE_OK, "shared reader");
        UmicomIpcModelBlock(other, shared, UMICOM_IPC_WAIT_FOREVER); /* Newer task registers first. */
        UmicomIpcModelBlock(receiver, receive, UMICOM_IPC_WAIT_FOREVER);
        if (!strcmp(name, "cancel-oldest"))
            UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, other) == UMICOM_USER_SCHEDULE_OK, "cancel oldest registration");
        UmicomIpcModelSend(sender, send, 0x31U); UmicomIpcModelPump();
        if (!strcmp(name, "fifo-waiters")) {
            UmicomModelRequire(UmicomModelInfo(other).state == UMICOM_USER_TASK_PAUSED &&
                UmicomModelInfo(receiver).state == UMICOM_USER_TASK_BLOCKED, "registration order, not slot order");
            UmicomIpcModelSend(sender, send, 0x32U); UmicomIpcModelPump();
        }
        UmicomModelRequire(UmicomModelInfo(receiver).state == UMICOM_USER_TASK_PAUSED, "eligible waiter completed"); return 1;
    }
    if (!strcmp(name, "repeated-lifetimes")) {
        UmicomIpcModelCleanup();
        for (unsigned i = 0U; i < 1000U; ++i) {
            receiver = UmicomModelCreate(64U); sender = UmicomModelCreate(64U);
            UmicomIpcModelPair(receiver, sender, &receive, &send);
            UmicomIpcModelBlock(receiver, receive, UMICOM_IPC_WAIT_FOREVER);
            UmicomIpcModelSend(sender, send, (UmicomU8)i); UmicomIpcModelPump();
            UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_OK, "reused lifetime completed");
            UmicomIpcModelCleanup();
        }
        return 1;
    }
    const int finite = !strcmp(name, "timeout") || !strcmp(name, "deadline-wins") || !strcmp(name, "before-deadline");
    if (!strcmp(name, "small-delivery")) {
        UmicomIpcModelCall(receiver, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, receive, UMICOM_IPC_MODEL_CROSS, 24U, UMICOM_IPC_WAIT_FOREVER);
    } else UmicomIpcModelBlock(receiver, receive, finite ? 500U : UMICOM_IPC_WAIT_FOREVER);
    if (!strcmp(name, "resume-once") || !strcmp(name, "no-query-right") || !strcmp(name, "maximum-copy")) {
        if (!strcmp(name, "no-query-right"))
            UmicomModelRequire(UmicomKernelMessageRestrict(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity,
                receive, UMICOM_MESSAGE_RIGHT_RECEIVE) == UMICOM_MESSAGE_OK, "query not required to receive");
        const UmicomU64 pc = UmicomModelInfo(receiver).resumePc, calls = UmicomModelInfo(receiver).systemCalls;
        if (!strcmp(name, "maximum-copy")) {
            UmicomIpcModelStore(sender, 0x59U, 256U);
            UmicomIpcModelCall(sender, UMICOM_USER_CALL_MESSAGE_SEND_WAIT, send, UMICOM_IPC_MODEL_CROSS, 256U, 0U);
        } else UmicomIpcModelSend(sender, send, 0x59U);
        UmicomIpcModelPump(); UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_OK &&
            UmicomModelInfo(receiver).resumePc == pc && UmicomModelInfo(receiver).systemCalls == calls &&
            umicomModelIpc.completed == 1U && UmicomIpcModelByte(receiver, UMICOM_IPC_MODEL_CROSS + 24U) == 0x59U,
            "same call resumes once across page boundary"); return 1;
    }
    if (finite) {
        if (strcmp(name, "timeout")) UmicomIpcModelSend(sender, send, 0x35U);
        umicomModelNow += !strcmp(name, "before-deadline") ? 499U : 500U;
        UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 ==
            (!strcmp(name, "before-deadline") ? UMICOM_MESSAGE_OK : UMICOM_IPC_RESULT_TIMED_OUT), "deadline order");
        if (!strcmp(name, "deadline-wins")) {
            UmicomKernelMessage packet;
            UmicomModelRequire(UmicomKernelMessageReceive(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity,
                receive, &packet, 256U) == UMICOM_MESSAGE_OK, "timeout did not consume queued packet");
        }
        return 1;
    }
    if (!strcmp(name, "spurious-pump")) {
        const unsigned entries = umicomModelEntries;
        for (unsigned i = 0U; i < 100U; ++i) UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelInfo(receiver).state == UMICOM_USER_TASK_BLOCKED && entries == umicomModelEntries,
            "pump invents no readiness");
        UmicomModelRequire(UmicomKernelUserTaskReap(&umicomModelScheduler, receiver) == UMICOM_USER_SCHEDULE_BAD_STATE,
            "blocked image cannot be freed"); return 1;
    }
    if (!strcmp(name, "cancel-and-reuse")) {
        const UmicomKernelUserTaskHandle old = receiver;
        UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, receiver) == UMICOM_USER_SCHEDULE_OK &&
            UmicomKernelUserTaskReap(&umicomModelScheduler, receiver) == UMICOM_USER_SCHEDULE_OK, "cancel and reap blocked task");
        receiver = UmicomModelCreate(64U); UmicomIpcModelPump();
        UmicomModelRequire(receiver != old && UmicomModelInfo(receiver).state == UMICOM_USER_TASK_READY &&
            UmicomModelRecord(receiver)->frame.x10_a0 == 17U &&
            UmicomKernelUserTaskCancel(&umicomModelScheduler, old) == UMICOM_USER_SCHEDULE_INVALID_HANDLE,
            "old completion cannot reach replacement"); return 1;
    }
    if (!strcmp(name, "peer-cancel") || !strcmp(name, "peer-fault") || !strcmp(name, "peer-exit") || !strcmp(name, "peer-budget")) {
        if (!strcmp(name, "peer-cancel"))
            UmicomModelRequire(UmicomKernelUserTaskCancel(&umicomModelScheduler, sender) == UMICOM_USER_SCHEDULE_OK, "cancel peer");
        else {
            umicomModelMode = !strcmp(name, "peer-exit") ? UMICOM_MODEL_EXIT :
                !strcmp(name, "peer-fault") ? UMICOM_MODEL_FAULT : UMICOM_MODEL_TIMER;
            if (!strcmp(name, "peer-budget")) UmicomModelRecord(sender)->sliceLimit = 1U;
            umicomModelScheduler.next = (UmicomU32)sender - 1U; (void)UmicomModelRun();
        }
        UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == UMICOM_MESSAGE_PEER_CLOSED, "terminal peer completes wait"); return 1;
    }
    if (!strcmp(name, "token-closed") || !strcmp(name, "right-revoked")) {
        const UmicomKernelMessageStatus status = !strcmp(name, "token-closed") ?
            UmicomKernelMessageClose(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity, receive) :
            UmicomKernelMessageRestrict(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity, receive, 0U);
        UmicomModelRequire(status == UMICOM_MESSAGE_OK, "change authority while suspended"); UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == (UmicomU64)(!strcmp(name, "token-closed") ?
            UMICOM_MESSAGE_INVALID_HANDLE : UMICOM_MESSAGE_ACCESS_DENIED), "authority rechecked at completion"); return 1;
    }
    if (!strcmp(name, "destination-unmapped") || !strcmp(name, "destination-readonly") || !strcmp(name, "small-delivery")) {
        UmicomIpcModelStore(receiver, 0xa7U, 16U);
        if (strcmp(name, "small-delivery")) {
            UmicomKernelUserTask *task = UmicomModelRecord(receiver); UmicomKernelUserPage *page = NULL;
            for (UmicomSize i = 0U; i < task->process.pageCount; ++i)
                if (task->process.pages[i].virtualBase == 0x601000U) page = &task->process.pages[i];
            UmicomModelRequire(page != NULL && UmicomKernelVirtualMemoryUnmapPage(&task->process.space, 0x601000U)
                == UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "remove second destination page");
            if (!strcmp(name, "destination-readonly"))
                UmicomModelRequire(UmicomKernelVirtualMemoryMapPage(&task->process.space, page->virtualBase, page->physicalBase,
                    UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_USER) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK,
                    "replace with read-only mapping");
        }
        UmicomIpcModelSend(sender, send, 0x45U); UmicomIpcModelPump();
        UmicomModelRequire(UmicomModelRecord(receiver)->frame.x10_a0 == (UmicomU64)(!strcmp(name, "small-delivery") ?
            UMICOM_MESSAGE_BUFFER_TOO_SMALL : UMICOM_MESSAGE_BAD_USER_BUFFER) &&
            UmicomIpcModelByte(receiver, UMICOM_IPC_MODEL_CROSS) == 0xa7U, "no partial first-page write");
        UmicomKernelMessage packet;
        UmicomModelRequire(UmicomKernelMessageReceive(&umicomModelIpc.messages, UmicomModelInfo(receiver).identity,
            receive, &packet, 256U) == UMICOM_MESSAGE_OK, "refused packet not consumed"); return 1;
    }
    if (!strcmp(name, "clock-reversal") || !strcmp(name, "corrupt-frame") || !strcmp(name, "corrupt-generation")) {
        if (!strcmp(name, "clock-reversal")) umicomModelNow = 1U;
        if (!strcmp(name, "corrupt-frame")) UmicomModelRecord(receiver)->frame.mepc = 0U;
        if (!strcmp(name, "corrupt-generation")) ++umicomModelIpc.waits[0].task;
        UmicomModelRequire(UmicomKernelUserIpcPump(&umicomModelScheduler) == UMICOM_FALSE &&
            umicomModelScheduler.poisoned != UMICOM_FALSE, "corruption poisons without admitting user execution");
        return 2; /* Retained ownership is intentionally not forced free. */
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    const int result = UmicomIpcModelCase(argv[1]);
    UmicomModelRequire(result != 0, "known blocking IPC case");
    if (result == 1) UmicomIpcModelCleanup();
    printf("PASS %s (%u checks)\n", argv[1], umicomModelChecks);
    return EXIT_SUCCESS;
}
