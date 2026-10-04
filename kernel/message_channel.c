/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/message_channel.c
 *
 * PURPOSE:
 *   Implement paired, copied-message queues with owner checks, shrinking rights
 *   and stale-handle rejection. No allocation or architecture instruction is
 *   needed to use a domain which already has Kernel-owned storage.
 *
 * EDUCATIONAL OVERVIEW:
 *   Each queue belongs to its receiving endpoint. Closing the last reference
 *   to that endpoint discards only its own incoming queue. Packets already sent
 *   to the other end remain readable there. The last close of both ends scrubs
 *   the channel so a later pair cannot inherit an earlier owner's messages.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/message_channel.h"

/* Volatile stores make scrubbing observable even if the compiler believes the
 * now-unreachable queue has no future reader. This is not a cache flush. */
static void UmicomMessageZero(void *storage, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)storage;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        output[index] = 0U;
    }
}
static void UmicomMessageCopy(void *destination, const void *source, UmicomSize bytes)
{
    UmicomU8 *const output = (UmicomU8 *)destination;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        output[index] = input[index];
    }
}

/* A caller must not use a result pointer or send buffer to overwrite registry
 * metadata. This checks arithmetic and overlap, not whether an arbitrary host
 * address is mapped. Readability remains the Kernel caller's contract. */
static UmicomBoolean UmicomMessageExternalSpan(
    const UmicomKernelMessageDomain *domain, const void *pointer, UmicomSize bytes)
{
    if (pointer == (const void *)0 || bytes == 0U) return UMICOM_FALSE;
    const UmicomAddress start = (UmicomAddress)pointer;
    const UmicomAddress base = (UmicomAddress)domain;
    const UmicomAddress maximum = ~(UmicomAddress)0U;
    if (start > maximum - (bytes - 1U) || base > maximum - (sizeof(*domain) - 1U)) {
        return UMICOM_FALSE;
    }
    const UmicomAddress last = start + bytes - 1U;
    const UmicomAddress domainLast = base + sizeof(*domain) - 1U;
    return last < base || start > domainLast ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelMessageStatus UmicomMessageDomainCheck(UmicomKernelMessageDomain *domain)
{
    if (domain == (UmicomKernelMessageDomain *)0) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    if (domain->initialised != UMICOM_TRUE) return UMICOM_MESSAGE_NOT_INITIALISED;
    return domain->self == domain ? UMICOM_MESSAGE_OK : UMICOM_MESSAGE_BAD_STATE;
}
static UmicomKernelMessageHandle UmicomMessageToken(UmicomU32 slot, UmicomU32 generation)
{
    /* Neither half wraps: index+1 is small, and generations retire at maximum. */
    return ((UmicomU64)generation << 32U) | ((UmicomU64)slot + 1U);
}
/* Decode the slot only after checking its range. Then check generation, owner
 * and rights before publishing an internal pointer. A token contains no secret;
 * the live, owner-bound table entry is the actual authority. */
static UmicomKernelMessageStatus UmicomMessageLookup(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageRights needed, UmicomKernelMessageReference **outReference)
{
    const UmicomKernelMessageStatus status = UmicomMessageDomainCheck(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (owner == 0U) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    const UmicomU32 slotWord = (UmicomU32)handle;
    const UmicomU32 generation = (UmicomU32)(handle >> 32U);
    if (slotWord == 0U || slotWord > UMICOM_MESSAGE_MAX_HANDLES || generation == 0U) {
        return UMICOM_MESSAGE_INVALID_HANDLE;
    }
    UmicomKernelMessageReference *const reference = &domain->handles[slotWord - 1U];
    if (reference->occupied != UMICOM_TRUE || reference->retired != UMICOM_FALSE ||
        reference->generation != generation) return UMICOM_MESSAGE_INVALID_HANDLE;
    if (reference->owner != owner) return UMICOM_MESSAGE_WRONG_OWNER;
    if (reference->channel >= UMICOM_MESSAGE_MAX_CHANNELS || reference->side > 1U ||
        (reference->rights & ~UMICOM_MESSAGE_RIGHT_ALL) != 0U) return UMICOM_MESSAGE_CORRUPT_STATE;
    UmicomKernelMessageChannel *const channel = &domain->channels[reference->channel];
    if (channel->occupied != UMICOM_TRUE || channel->references[reference->side] == 0U) {
        return UMICOM_MESSAGE_CORRUPT_STATE;
    }
    if ((reference->rights & needed) != needed) return UMICOM_MESSAGE_ACCESS_DENIED;
    *outReference = reference;
    return UMICOM_MESSAGE_OK;
}
/* Quotas count references, not channel objects. Granting a new reference must
 * charge the receiving owner even when no queue storage is allocated. */
static UmicomSize UmicomMessageOwnerCount(const UmicomKernelMessageDomain *domain, UmicomU64 owner)
{
    UmicomSize count = 0U;
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        if (domain->handles[index].occupied == UMICOM_TRUE && domain->handles[index].owner == owner) ++count;
    }
    return count;
}
/* A retired slot is permanently unavailable. Skipping it is intentional: a
 * wrapped generation could make an arbitrarily old handle valid again. */
static UmicomU32 UmicomMessageFreeHandle(const UmicomKernelMessageDomain *domain, UmicomU32 exclude)
{
    for (UmicomU32 index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        const UmicomKernelMessageReference *const reference = &domain->handles[index];
        if (index != exclude && reference->occupied == UMICOM_FALSE && reference->retired == UMICOM_FALSE) {
            return index;
        }
    }
    return UMICOM_MESSAGE_MAX_HANDLES;
}
static void UmicomMessagePublish(UmicomKernelMessageDomain *domain, UmicomU32 slot,
    UmicomU32 channel, UmicomU32 side, UmicomU64 owner, UmicomKernelMessageRights rights)
{
    UmicomKernelMessageReference *const reference = &domain->handles[slot];
    reference->channel = channel;
    reference->side = side;
    reference->owner = owner;
    reference->rights = rights;
    reference->occupied = UMICOM_TRUE;
    ++domain->channels[channel].references[side];
}
UmicomKernelMessageStatus UmicomKernelMessageInitialize(UmicomKernelMessageDomain *domain)
{
    if (domain == (UmicomKernelMessageDomain *)0) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    /* Initialisation is a one-way event. Reading bytes also detects dirty
     * storage without interpreting a partially initialised pointer as authority. */
    const UmicomU8 *const bytes = (const UmicomU8 *)domain;
    for (UmicomSize index = 0U; index < sizeof(*domain); ++index) {
        if (bytes[index] != 0U) return UMICOM_MESSAGE_BAD_STATE;
    }
    domain->self = domain;
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        domain->handles[index].generation = 1U;
    }
    domain->initialised = UMICOM_TRUE;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessagePairCreate(
    UmicomKernelMessageDomain *domain,
    UmicomU64 firstOwner, UmicomKernelMessageRights firstRights,
    UmicomU64 secondOwner, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond)
{
    const UmicomKernelMessageStatus status = UmicomMessageDomainCheck(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (firstOwner == 0U || secondOwner == 0U ||
        ((firstRights | secondRights) & ~UMICOM_MESSAGE_RIGHT_ALL) != 0U ||
        UmicomMessageExternalSpan(domain, outFirst, sizeof(*outFirst)) == UMICOM_FALSE ||
        UmicomMessageExternalSpan(domain, outSecond, sizeof(*outSecond)) == UMICOM_FALSE) {
        return UMICOM_MESSAGE_INVALID_ARGUMENT;
    }
    const UmicomAddress first = (UmicomAddress)outFirst;
    const UmicomAddress second = (UmicomAddress)outSecond;
    if (first <= second + sizeof(*outSecond) - 1U && second <= first + sizeof(*outFirst) - 1U) {
        return UMICOM_MESSAGE_INVALID_ARGUMENT;
    }
    /* A same-owner pair consumes two references, not two separate quota checks
     * against a count which has not changed yet. Admission is all-or-nothing. */
    const UmicomSize firstNeed = firstOwner == secondOwner ? 2U : 1U;
    if (UmicomMessageOwnerCount(domain, firstOwner) > UMICOM_MESSAGE_MAX_OWNER_HANDLES - firstNeed ||
        UmicomMessageOwnerCount(domain, secondOwner) >= UMICOM_MESSAGE_MAX_OWNER_HANDLES) {
        return UMICOM_MESSAGE_OWNER_LIMIT;
    }
    const UmicomU32 firstSlot = UmicomMessageFreeHandle(domain, UMICOM_MESSAGE_MAX_HANDLES);
    const UmicomU32 secondSlot = UmicomMessageFreeHandle(domain, firstSlot);
    if (firstSlot == UMICOM_MESSAGE_MAX_HANDLES || secondSlot == UMICOM_MESSAGE_MAX_HANDLES) {
        return UMICOM_MESSAGE_HANDLE_LIMIT;
    }
    UmicomU32 channelIndex = 0U;
    while (channelIndex < UMICOM_MESSAGE_MAX_CHANNELS && domain->channels[channelIndex].occupied != UMICOM_FALSE) {
        ++channelIndex;
    }
    if (channelIndex == UMICOM_MESSAGE_MAX_CHANNELS) return UMICOM_MESSAGE_CHANNEL_LIMIT;
    UmicomKernelMessageChannel *const channel = &domain->channels[channelIndex];
    /* No allocation can fail beyond this commit point. Queues start scrubbed. */
    UmicomMessageZero(channel, sizeof(*channel));
    channel->occupied = UMICOM_TRUE;
    channel->nextSequence = 1U;
    UmicomMessagePublish(domain, firstSlot, channelIndex, 0U, firstOwner, firstRights);
    UmicomMessagePublish(domain, secondSlot, channelIndex, 1U, secondOwner, secondRights);
    *outFirst = UmicomMessageToken(firstSlot, domain->handles[firstSlot].generation);
    *outSecond = UmicomMessageToken(secondSlot, domain->handles[secondSlot].generation);
    return UMICOM_MESSAGE_OK;
}
static UmicomKernelMessageStatus UmicomMessageQueueCheck(const UmicomKernelMessageQueue *queue)
{
    return queue->head < UMICOM_MESSAGE_QUEUE_DEPTH && queue->count <= UMICOM_MESSAGE_QUEUE_DEPTH
        ? UMICOM_MESSAGE_OK : UMICOM_MESSAGE_CORRUPT_STATE;
}
UmicomKernelMessageStatus UmicomKernelMessageSend(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    const UmicomU8 *data, UmicomSize bytes)
{
    UmicomKernelMessageReference *reference = (UmicomKernelMessageReference *)0;
    UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, UMICOM_MESSAGE_RIGHT_SEND, &reference);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (bytes > UMICOM_MESSAGE_MAX_BYTES) return UMICOM_MESSAGE_TOO_LARGE;
    if (UmicomMessageExternalSpan(domain, data, bytes) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    UmicomKernelMessageChannel *const channel = &domain->channels[reference->channel];
    const UmicomU32 peer = reference->side ^ 1U;
    if (channel->references[peer] == 0U) return UMICOM_MESSAGE_PEER_CLOSED;
    UmicomKernelMessageQueue *const queue = &channel->incoming[peer];
    status = UmicomMessageQueueCheck(queue);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (queue->count == UMICOM_MESSAGE_QUEUE_DEPTH) return UMICOM_MESSAGE_WOULD_BLOCK;
    if (channel->nextSequence == 0U) return UMICOM_MESSAGE_SEQUENCE_EXHAUSTED;
    const UmicomSize tail = (queue->head + queue->count) % UMICOM_MESSAGE_QUEUE_DEPTH;
    UmicomKernelMessage *const message = &queue->slots[tail];
    /* The tail is not visible to the receiver until count increases. */
    UmicomMessageZero(message, sizeof(*message));
    UmicomMessageCopy(message->data, data, bytes);
    message->sender = owner;
    message->sequence = channel->nextSequence;
    message->bytes = bytes;
    channel->nextSequence = channel->nextSequence == ~(UmicomU64)0U ? 0U : channel->nextSequence + 1U;
    ++queue->count;
    return UMICOM_MESSAGE_OK;
}
/* Empty and closed are different. A closed sender does not revoke packets
 * which were accepted while its endpoint was alive; inspect the queue first. */
static UmicomKernelMessageStatus UmicomMessageFront(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageQueue **outQueue)
{
    UmicomKernelMessageReference *reference = (UmicomKernelMessageReference *)0;
    UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, UMICOM_MESSAGE_RIGHT_RECEIVE, &reference);
    if (status != UMICOM_MESSAGE_OK) return status;
    UmicomKernelMessageChannel *const channel = &domain->channels[reference->channel];
    UmicomKernelMessageQueue *const queue = &channel->incoming[reference->side];
    status = UmicomMessageQueueCheck(queue);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (queue->count == 0U) {
        return channel->references[reference->side ^ 1U] == 0U
            ? UMICOM_MESSAGE_PEER_CLOSED : UMICOM_MESSAGE_WOULD_BLOCK;
    }
    const UmicomKernelMessage *const message = &queue->slots[queue->head];
    if (message->bytes == 0U || message->bytes > UMICOM_MESSAGE_MAX_BYTES ||
        message->sequence == 0U || message->sender == 0U) return UMICOM_MESSAGE_CORRUPT_STATE;
    *outQueue = queue;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessagePeek(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessage *outMessage)
{
    UmicomKernelMessageQueue *queue = (UmicomKernelMessageQueue *)0;
    const UmicomKernelMessageStatus status = UmicomMessageFront(domain, owner, handle, &queue);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (UmicomMessageExternalSpan(domain, outMessage, sizeof(*outMessage)) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    UmicomMessageCopy(outMessage, &queue->slots[queue->head], sizeof(*outMessage));
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageConsume(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomU64 expectedSequence)
{
    UmicomKernelMessageQueue *queue = (UmicomKernelMessageQueue *)0;
    const UmicomKernelMessageStatus status = UmicomMessageFront(domain, owner, handle, &queue);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (queue->slots[queue->head].sequence != expectedSequence) return UMICOM_MESSAGE_STATE_CHANGED;
    /* Retire bytes before recycling their ring slot. No later receiver sees
     * old payload padding even if its own packet is shorter. */
    UmicomMessageZero(&queue->slots[queue->head], sizeof(queue->slots[queue->head]));
    queue->head = (queue->head + 1U) % UMICOM_MESSAGE_QUEUE_DEPTH;
    --queue->count;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageReceive(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessage *outMessage, UmicomSize payloadCapacity)
{
    UmicomKernelMessageQueue *queue = (UmicomKernelMessageQueue *)0;
    const UmicomKernelMessageStatus status = UmicomMessageFront(domain, owner, handle, &queue);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (UmicomMessageExternalSpan(domain, outMessage, sizeof(*outMessage)) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    if (payloadCapacity < queue->slots[queue->head].bytes) return UMICOM_MESSAGE_BUFFER_TOO_SMALL;
    UmicomMessageCopy(outMessage, &queue->slots[queue->head], sizeof(*outMessage));
    return UmicomKernelMessageConsume(domain, owner, handle, outMessage->sequence);
}
UmicomKernelMessageStatus UmicomKernelMessageQuery(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageInfo *outInfo)
{
    UmicomKernelMessageReference *reference = (UmicomKernelMessageReference *)0;
    UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, UMICOM_MESSAGE_RIGHT_QUERY, &reference);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (UmicomMessageExternalSpan(domain, outInfo, sizeof(*outInfo)) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    UmicomKernelMessageChannel *const channel = &domain->channels[reference->channel];
    const UmicomKernelMessageQueue *const queue = &channel->incoming[reference->side];
    const UmicomKernelMessageQueue *const peerQueue = &channel->incoming[reference->side ^ 1U];
    if (UmicomMessageQueueCheck(queue) != UMICOM_MESSAGE_OK || UmicomMessageQueueCheck(peerQueue) != UMICOM_MESSAGE_OK) {
        return UMICOM_MESSAGE_CORRUPT_STATE;
    }
    UmicomU64 flags = 0U;
    if (queue->count != 0U && (reference->rights & UMICOM_MESSAGE_RIGHT_RECEIVE) != 0U) flags |= UMICOM_MESSAGE_READABLE;
    if (channel->references[reference->side ^ 1U] == 0U) flags |= UMICOM_MESSAGE_PEER_GONE;
    else if (peerQueue->count < UMICOM_MESSAGE_QUEUE_DEPTH && channel->nextSequence != 0U &&
        (reference->rights & UMICOM_MESSAGE_RIGHT_SEND) != 0U) flags |= UMICOM_MESSAGE_WRITABLE;
    outInfo->rights = reference->rights;
    outInfo->queued = queue->count;
    outInfo->nextBytes = queue->count == 0U ? 0U : queue->slots[queue->head].bytes;
    outInfo->flags = flags;
    outInfo->endpointReferences = channel->references[reference->side];
    return UMICOM_MESSAGE_OK;
}
/* Duplication shares an endpoint, not its payload buffer. Transfer authority
 * permits a new owner-bound reference, while the subset test prevents any new
 * reference from gaining rights which the source did not possess. */
static UmicomKernelMessageStatus UmicomMessageDuplicateTo(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomU64 receivingOwner, UmicomKernelMessageRights rights, UmicomBoolean transfer,
    UmicomKernelMessageHandle *outHandle)
{
    UmicomKernelMessageReference *source = (UmicomKernelMessageReference *)0;
    const UmicomKernelMessageRights needed = UMICOM_MESSAGE_RIGHT_DUPLICATE |
        (transfer != UMICOM_FALSE ? UMICOM_MESSAGE_RIGHT_TRANSFER : 0U);
    const UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, needed, &source);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (receivingOwner == 0U || (rights & ~UMICOM_MESSAGE_RIGHT_ALL) != 0U ||
        UmicomMessageExternalSpan(domain, outHandle, sizeof(*outHandle)) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    if ((rights & ~source->rights) != 0U) return UMICOM_MESSAGE_ACCESS_DENIED;
    if (UmicomMessageOwnerCount(domain, receivingOwner) >= UMICOM_MESSAGE_MAX_OWNER_HANDLES) return UMICOM_MESSAGE_OWNER_LIMIT;
    const UmicomU32 slot = UmicomMessageFreeHandle(domain, UMICOM_MESSAGE_MAX_HANDLES);
    if (slot == UMICOM_MESSAGE_MAX_HANDLES) return UMICOM_MESSAGE_HANDLE_LIMIT;
    if (domain->channels[source->channel].references[source->side] >= UMICOM_MESSAGE_MAX_HANDLES) return UMICOM_MESSAGE_CORRUPT_STATE;
    UmicomMessagePublish(domain, slot, source->channel, source->side, receivingOwner, rights);
    *outHandle = UmicomMessageToken(slot, domain->handles[slot].generation);
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageDuplicate(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageRights rights, UmicomKernelMessageHandle *outHandle)
{
    return UmicomMessageDuplicateTo(domain, owner, handle, owner, rights, UMICOM_FALSE, outHandle);
}
UmicomKernelMessageStatus UmicomKernelMessageGrant(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomU64 receivingOwner, UmicomKernelMessageRights rights, UmicomKernelMessageHandle *outHandle)
{
    return UmicomMessageDuplicateTo(domain, owner, handle, receivingOwner, rights, UMICOM_TRUE, outHandle);
}
UmicomKernelMessageStatus UmicomKernelMessageRestrict(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle,
    UmicomKernelMessageRights rights)
{
    UmicomKernelMessageReference *reference = (UmicomKernelMessageReference *)0;
    const UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, 0U, &reference);
    if (status != UMICOM_MESSAGE_OK) return status;
    if ((rights & ~UMICOM_MESSAGE_RIGHT_ALL) != 0U) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    if ((rights & ~reference->rights) != 0U) return UMICOM_MESSAGE_ACCESS_DENIED;
    reference->rights = rights;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageClose(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomKernelMessageHandle handle)
{
    UmicomKernelMessageReference *reference = (UmicomKernelMessageReference *)0;
    const UmicomKernelMessageStatus status = UmicomMessageLookup(domain, owner, handle, 0U, &reference);
    if (status != UMICOM_MESSAGE_OK) return status;
    UmicomKernelMessageChannel *const channel = &domain->channels[reference->channel];
    --channel->references[reference->side];
    if (channel->references[reference->side] == 0U) {
        /* Nobody can receive at this end now. Discard messages addressed to
         * it, but leave the other queue available for the surviving peer. */
        UmicomMessageZero(&channel->incoming[reference->side], sizeof(channel->incoming[reference->side]));
    }
    if (channel->references[0] == 0U && channel->references[1] == 0U) {
        UmicomMessageZero(channel, sizeof(*channel));
    }
    reference->occupied = UMICOM_FALSE;
    reference->owner = 0U;
    reference->rights = 0U;
    reference->channel = 0U;
    reference->side = 0U;
    if (reference->generation == ~(UmicomU32)0U) reference->retired = UMICOM_TRUE;
    else ++reference->generation;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageCloseOwner(
    UmicomKernelMessageDomain *domain, UmicomU64 owner, UmicomSize *outClosed)
{
    const UmicomKernelMessageStatus status = UmicomMessageDomainCheck(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (owner == 0U || UmicomMessageExternalSpan(domain, outClosed, sizeof(*outClosed)) == UMICOM_FALSE) {
        return UMICOM_MESSAGE_INVALID_ARGUMENT;
    }
    *outClosed = 0U;
    for (UmicomU32 index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        UmicomKernelMessageReference *const reference = &domain->handles[index];
        if (reference->occupied == UMICOM_TRUE && reference->owner == owner) {
            const UmicomKernelMessageStatus closed = UmicomKernelMessageClose(domain, owner,
                UmicomMessageToken(index, reference->generation));
            if (closed != UMICOM_MESSAGE_OK) return closed;
            ++*outClosed;
        }
    }
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageValidate(UmicomKernelMessageDomain *domain)
{
    const UmicomKernelMessageStatus status = UmicomMessageDomainCheck(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    /* Avoid an aggregate initializer which a freestanding compiler may lower
     * to hosted memset. Our explicit scrub helper needs no runtime library. */
    UmicomSize observed[UMICOM_MESSAGE_MAX_CHANNELS][2];
    UmicomMessageZero(observed, sizeof(observed));
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        const UmicomKernelMessageReference *const reference = &domain->handles[index];
        if (reference->generation == 0U ||
            (reference->retired != UMICOM_FALSE && reference->retired != UMICOM_TRUE)) {
            return UMICOM_MESSAGE_CORRUPT_STATE;
        }
        if (reference->occupied == UMICOM_FALSE) {
            if (reference->owner != 0U || reference->rights != 0U || reference->channel != 0U || reference->side != 0U ||
                (reference->retired != UMICOM_FALSE && reference->generation != ~(UmicomU32)0U)) return UMICOM_MESSAGE_CORRUPT_STATE;
            continue;
        }
        if (reference->occupied != UMICOM_TRUE || reference->retired != UMICOM_FALSE ||
            reference->owner == 0U || reference->side > 1U || reference->channel >= UMICOM_MESSAGE_MAX_CHANNELS ||
            (reference->rights & ~UMICOM_MESSAGE_RIGHT_ALL) != 0U) return UMICOM_MESSAGE_CORRUPT_STATE;
        if (UmicomMessageOwnerCount(domain, reference->owner) > UMICOM_MESSAGE_MAX_OWNER_HANDLES) return UMICOM_MESSAGE_CORRUPT_STATE;
        ++observed[reference->channel][reference->side];
    }
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_CHANNELS; ++index) {
        const UmicomKernelMessageChannel *const channel = &domain->channels[index];
        if (channel->occupied != UMICOM_FALSE && channel->occupied != UMICOM_TRUE) {
            return UMICOM_MESSAGE_CORRUPT_STATE;
        }
        /* An unused channel was scrubbed at its last close. Recounting only
         * references would miss leftover bytes in a supposedly empty object. */
        if (channel->occupied == UMICOM_FALSE) {
            const UmicomU8 *const storage = (const UmicomU8 *)channel;
            for (UmicomSize byte = 0U; byte < sizeof(*channel); ++byte) {
                if (storage[byte] != 0U) return UMICOM_MESSAGE_CORRUPT_STATE;
            }
        }
        if (channel->references[0] != observed[index][0] || channel->references[1] != observed[index][1]) return UMICOM_MESSAGE_CORRUPT_STATE;
        if ((channel->occupied == UMICOM_TRUE) != (observed[index][0] + observed[index][1] != 0U)) return UMICOM_MESSAGE_CORRUPT_STATE;
        for (UmicomSize side = 0U; side < 2U; ++side) {
            const UmicomKernelMessageQueue *const queue = &channel->incoming[side];
            if (UmicomMessageQueueCheck(queue) != UMICOM_MESSAGE_OK ||
                (channel->references[side] == 0U && queue->count != 0U)) return UMICOM_MESSAGE_CORRUPT_STATE;
            UmicomU64 previous = 0U;
            for (UmicomSize offset = 0U; offset < queue->count; ++offset) {
                const UmicomKernelMessage *const message = &queue->slots[(queue->head + offset) % UMICOM_MESSAGE_QUEUE_DEPTH];
                if (message->sender == 0U || message->bytes == 0U || message->bytes > UMICOM_MESSAGE_MAX_BYTES ||
                    message->sequence <= previous ||
                    (channel->nextSequence != 0U && message->sequence >= channel->nextSequence)) return UMICOM_MESSAGE_CORRUPT_STATE;
                previous = message->sequence;
                for (UmicomSize byte = message->bytes; byte < UMICOM_MESSAGE_MAX_BYTES; ++byte) {
                    if (message->data[byte] != 0U) return UMICOM_MESSAGE_CORRUPT_STATE;
                }
            }
        }
    }
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageSnapshotRead(
    UmicomKernelMessageDomain *domain, UmicomKernelMessageSnapshot *outSnapshot)
{
    const UmicomKernelMessageStatus status = UmicomKernelMessageValidate(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    if (UmicomMessageExternalSpan(domain, outSnapshot, sizeof(*outSnapshot)) == UMICOM_FALSE) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    UmicomKernelMessageSnapshot result = { 0U, 0U, 0U, 0U };
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_CHANNELS; ++index) {
        if (domain->channels[index].occupied == UMICOM_TRUE) ++result.channels;
        result.messages += domain->channels[index].incoming[0].count + domain->channels[index].incoming[1].count;
    }
    for (UmicomSize index = 0U; index < UMICOM_MESSAGE_MAX_HANDLES; ++index) {
        if (domain->handles[index].occupied == UMICOM_TRUE) ++result.handles;
        if (domain->handles[index].retired == UMICOM_TRUE) ++result.retiredHandles;
    }
    *outSnapshot = result;
    return UMICOM_MESSAGE_OK;
}
const char *UmicomKernelMessageStatusName(UmicomKernelMessageStatus status)
{
    switch (status) {
        case UMICOM_MESSAGE_OK: return "ok";
        case UMICOM_MESSAGE_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_MESSAGE_NOT_INITIALISED: return "not-initialised";
        case UMICOM_MESSAGE_BAD_STATE: return "bad-state";
        case UMICOM_MESSAGE_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_MESSAGE_WRONG_OWNER: return "wrong-owner";
        case UMICOM_MESSAGE_ACCESS_DENIED: return "access-denied";
        case UMICOM_MESSAGE_CHANNEL_LIMIT: return "channel-limit";
        case UMICOM_MESSAGE_HANDLE_LIMIT: return "handle-limit";
        case UMICOM_MESSAGE_OWNER_LIMIT: return "owner-limit";
        case UMICOM_MESSAGE_TOO_LARGE: return "too-large";
        case UMICOM_MESSAGE_WOULD_BLOCK: return "would-block";
        case UMICOM_MESSAGE_PEER_CLOSED: return "peer-closed";
        case UMICOM_MESSAGE_BUFFER_TOO_SMALL: return "buffer-too-small";
        case UMICOM_MESSAGE_SEQUENCE_EXHAUSTED: return "sequence-exhausted";
        case UMICOM_MESSAGE_STATE_CHANGED: return "state-changed";
        case UMICOM_MESSAGE_BAD_USER_BUFFER: return "bad-user-buffer";
        case UMICOM_MESSAGE_SERVICE_UNBOUND: return "service-unbound";
        case UMICOM_MESSAGE_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-message-status";
    }
}
