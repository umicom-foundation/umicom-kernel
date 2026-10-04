/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/message_channel.h
 *
 * PURPOSE:
 *   Own paired message endpoints, their bounded queues and rights-checked
 *   references without lending a receiver access to a sender's memory.
 *
 * EDUCATIONAL OVERVIEW:
 *   A channel has two ends and two incoming queues. Sending at one end copies
 *   a packet into the other end's queue. Once accepted, that packet no longer
 *   borrows the sender's buffer. Closing a reference is not necessarily closing
 *   an endpoint: another reference may still keep that endpoint alive.
 *
 *   Storage is fixed and Kernel-owned. All calls are serialised on one hart.
 *   There are no locks, blocking waits, scheduler integration or handle passing
 *   inside messages yet. Process handles and message handles are distinct
 *   domains even when their integer encodings happen to be equal.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_MESSAGE_CHANNEL_H
#define UMICOM_KERNEL_MESSAGE_CHANNEL_H
#include "umicom/kernel/message_abi.h"

#define UMICOM_MESSAGE_MAX_CHANNELS 8U
#define UMICOM_MESSAGE_MAX_HANDLES 32U
#define UMICOM_MESSAGE_MAX_OWNER_HANDLES 8U

/* These fields are public only to permit static allocation and native fault
 * injection tests. Clients must not edit, copy or reset a live domain. */
typedef struct UmicomKernelMessageQueue {
    UmicomSize head;
    UmicomSize count;
    UmicomKernelMessage slots[UMICOM_MESSAGE_QUEUE_DEPTH];
} UmicomKernelMessageQueue;
typedef struct UmicomKernelMessageChannel {
    UmicomBoolean occupied;
    UmicomSize references[2];
    UmicomU64 nextSequence; /* Zero means exhausted, not reusable. */
    UmicomKernelMessageQueue incoming[2];
} UmicomKernelMessageChannel;
typedef struct UmicomKernelMessageReference {
    UmicomU64 owner;
    UmicomU32 generation;
    UmicomU32 channel;
    UmicomU32 side;
    UmicomKernelMessageRights rights;
    UmicomBoolean occupied;
    UmicomBoolean retired;
} UmicomKernelMessageReference;
typedef struct UmicomKernelMessageDomain {
    const struct UmicomKernelMessageDomain *self;
    UmicomBoolean initialised;
    UmicomKernelMessageChannel channels[UMICOM_MESSAGE_MAX_CHANNELS];
    UmicomKernelMessageReference handles[UMICOM_MESSAGE_MAX_HANDLES];
} UmicomKernelMessageDomain;
typedef struct UmicomKernelMessageSnapshot {
    UmicomSize channels;
    UmicomSize handles;
    UmicomSize retiredHandles;
    UmicomSize messages;
} UmicomKernelMessageSnapshot;

/* Initialise zero-filled stable storage once. Reinitialisation could make old
 * tokens valid again, so even an empty live domain cannot be reset. All pointers
 * below are Kernel-owned, and must not overlap the domain. No user pointer is
 * permitted here; the syscall adapter performs that separate validation. */
UmicomKernelMessageStatus UmicomKernelMessageInitialize(UmicomKernelMessageDomain *domain);
UmicomKernelMessageStatus UmicomKernelMessagePairCreate(
    UmicomKernelMessageDomain *domain,
    UmicomU64 firstOwner, UmicomKernelMessageRights firstRights,
    UmicomU64 secondOwner, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond);

/* A send either queues the entire copied payload or changes nothing. Queue
 * exhaustion never evicts an older message. A receive never truncates a packet. */
UmicomKernelMessageStatus UmicomKernelMessageSend(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    const UmicomU8 *data, UmicomSize bytes);
UmicomKernelMessageStatus UmicomKernelMessageReceive(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessage *outMessage, UmicomSize payloadCapacity);

/* Peek/Consume form the adapter's receive transaction. It can validate and copy
 * the whole user destination before removing a packet. Consume accepts only
 * the expected front sequence. No other consumer or mapper may run between
 * the two operations; these calls are not a concurrent reservation protocol. */
UmicomKernelMessageStatus UmicomKernelMessagePeek(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessage *outMessage);
UmicomKernelMessageStatus UmicomKernelMessageConsume(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomU64 expectedSequence);
UmicomKernelMessageStatus UmicomKernelMessageQuery(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageInfo *outInfo);

/* Grants issue a new owner-bound reference. They neither transfer payload
 * pointers nor silently revoke the sender. Rights can only stay equal or shrink. */
UmicomKernelMessageStatus UmicomKernelMessageDuplicate(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageRights rights, UmicomKernelMessageHandle *outHandle);
UmicomKernelMessageStatus UmicomKernelMessageGrant(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomU64 receivingOwner, UmicomKernelMessageRights rights, UmicomKernelMessageHandle *outHandle);
UmicomKernelMessageStatus UmicomKernelMessageRestrict(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageRights rights);
UmicomKernelMessageStatus UmicomKernelMessageClose(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle);
UmicomKernelMessageStatus UmicomKernelMessageCloseOwner(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomSize *outClosed);
UmicomKernelMessageStatus UmicomKernelMessageValidate(UmicomKernelMessageDomain *domain);
UmicomKernelMessageStatus UmicomKernelMessageSnapshotRead(
    UmicomKernelMessageDomain *domain, UmicomKernelMessageSnapshot *outSnapshot);
const char *UmicomKernelMessageStatusName(UmicomKernelMessageStatus status);
void UmicomKernelMessageChannelsValidateExecution(void);
#endif /* UMICOM_KERNEL_MESSAGE_CHANNEL_H */
