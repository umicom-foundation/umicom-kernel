/*-----------------------------------------------------------------------------
 * Umicom Kernel task-owned standard streams
 * File: include/umicom/kernel/user_streams.h
 *
 * The owner keeps copied input, ordered output records and any unfinished call
 * outside user memory. A stopped process may still have accepted output waiting
 * to be drained. Reaping must not quietly discard that output.
 *
 * All calls are serial on hart zero. Storage starts fully zero-filled, remains
 * at a stable address and outlives its attached scheduler. No allocation, UART
 * access, terminal callback or context switch is performed inside a stream call.
 * This is neither an IRQ-safe queue nor an SMP synchronisation primitive.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_STREAMS_H
#define UMICOM_KERNEL_USER_STREAMS_H
#include "umicom/kernel/user_scheduler.h"
#include "umicom/kernel/stream_abi.h"

#define UMICOM_STREAM_INPUT_BYTES 1024U
#define UMICOM_STREAM_OUTPUT_RECORDS 8U

typedef struct UmicomKernelStreamPacket {
    UmicomU64 selector; /* OUTPUT or ERROR; one queue preserves their write order. */
    UmicomSize bytes;
    UmicomU8 data[UMICOM_STREAM_TRANSFER_BYTES];
} UmicomKernelStreamPacket;
typedef enum UmicomKernelStreamWait {
    UMICOM_STREAM_WAIT_NONE,
    UMICOM_STREAM_WAIT_INPUT,
    UMICOM_STREAM_WAIT_OUTPUT
} UmicomKernelStreamWait;

/* Visible for static allocation and fault injection, not direct client edits. */
typedef struct UmicomKernelUserStreamRecord {
    UmicomKernelUserTaskHandle task;
    UmicomU64 identity;
    UmicomU8 input[UMICOM_STREAM_INPUT_BYTES];
    UmicomSize inputHead;
    UmicomSize inputCount;
    UmicomKernelStreamPacket output[UMICOM_STREAM_OUTPUT_RECORDS];
    UmicomSize outputHead;
    UmicomSize outputCount;
    UmicomBoolean inputEnded;
    UmicomBoolean stopped;
    UmicomKernelStreamWait wait;
    UmicomAddress address; /* A READ address is revalidated before consuming input. */
    UmicomAddress nextPc;
    UmicomSize bytes;
    UmicomU64 selector;
    UmicomU8 pending[UMICOM_STREAM_TRANSFER_BYTES]; /* Snapshot of an unaccepted WRITE. */
} UmicomKernelUserStreamRecord;
typedef struct UmicomKernelUserStreams {
    const struct UmicomKernelUserStreams *self;
    UmicomKernelUserScheduler *scheduler;
    UmicomBoolean initialised;
    UmicomBoolean closed;
    UmicomBoolean poisoned;
    UmicomKernelUserStreamRecord records[UMICOM_USER_TASK_LIMIT];
} UmicomKernelUserStreams;
typedef struct UmicomKernelStreamInfo {
    UmicomSize inputBytes;
    UmicomSize outputRecords;
    UmicomBoolean inputEnded;
    UmicomBoolean stopped;
    UmicomKernelStreamWait wait;
} UmicomKernelStreamInfo;

/* Attach once before task admission; Grant is explicit for each fresh task. */
UmicomKernelStreamStatus UmicomKernelUserStreamsAttach(UmicomKernelUserStreams *streams,
    UmicomKernelUserScheduler *scheduler);
UmicomKernelStreamStatus UmicomKernelUserStreamsGrant(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task);
/* Input is all-or-nothing. A full ring does not silently accept a prefix of a
 * line. EndInput is sticky for this invocation and does not discard queued data. */
UmicomKernelStreamStatus UmicomKernelUserStreamsInput(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task, const UmicomU8 *data, UmicomSize bytes);
UmicomKernelStreamStatus UmicomKernelUserStreamsEndInput(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task);
/* Drain returns exactly one copied output record and removes it. No user
 * pointer remains involved, even if its producer has already stopped. */
UmicomKernelStreamStatus UmicomKernelUserStreamsDrain(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task, UmicomKernelStreamPacket *out);
UmicomKernelStreamStatus UmicomKernelUserStreamsQuery(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task, UmicomKernelStreamInfo *out);
/* Explicitly abandoning accepted output is allowed only for a safely stopped
 * task. Ordinary Reap refuses undrained output instead of calling this itself. */
UmicomKernelStreamStatus UmicomKernelUserStreamsDiscard(UmicomKernelUserStreams *streams,
    UmicomKernelUserTaskHandle task);
UmicomKernelStreamStatus UmicomKernelUserStreamsClose(UmicomKernelUserStreams *streams);

/* Scheduler/trap hooks. A null attachment keeps all existing execution paths. */
UmicomBoolean UmicomKernelUserStreamsPump(UmicomKernelUserScheduler *scheduler);
UmicomBoolean UmicomKernelUserStreamsBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserStreamsEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserStreamsWaiting(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomKernelUserScheduleStatus UmicomKernelUserStreamsSuspend(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired);
UmicomBoolean UmicomKernelUserStreamsStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserStreamsReap(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserStreamsValidate(UmicomKernelUserScheduler *scheduler);
UmicomU64 UmicomKernelUserStreamDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc);
void UmicomKernelStandardStreamsValidateExecution(void);
#endif /* UMICOM_KERNEL_USER_STREAMS_H */
