/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/message_abi.h
 *
 * PURPOSE:
 *   Describe bounded message operations shared by native programs and the
 *   Kernel. These numbers select services, not releases or development stages.
 *
 * EDUCATIONAL OVERVIEW:
 *   The caller supplies an endpoint token, never a process identity or Kernel
 *   address. The monitor supplies the authenticated identity from its session.
 *   All calls return a status in a0; received bytes and metadata are written
 *   to a checked user buffer. Other saved registers retain their original values.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_MESSAGE_ABI_H
#define UMICOM_KERNEL_MESSAGE_ABI_H
#include "umicom/kernel/types.h"

/* The established EXIT, IDENTITY and COPY service numbers remain unchanged. */
#define UMICOM_USER_CALL_MESSAGE_SEND 16U
#define UMICOM_USER_CALL_MESSAGE_RECEIVE 17U
#define UMICOM_USER_CALL_MESSAGE_QUERY 18U
#define UMICOM_USER_CALL_MESSAGE_CLOSE 19U

/* Bounded payloads keep both queue storage and time spent in a call predictable. */
#define UMICOM_MESSAGE_MAX_BYTES 256U
#define UMICOM_MESSAGE_QUEUE_DEPTH 4U

typedef UmicomU64 UmicomKernelMessageHandle;
typedef UmicomU32 UmicomKernelMessageRights;
#define UMICOM_MESSAGE_RIGHT_SEND ((UmicomKernelMessageRights)1U)
#define UMICOM_MESSAGE_RIGHT_RECEIVE ((UmicomKernelMessageRights)2U)
#define UMICOM_MESSAGE_RIGHT_QUERY ((UmicomKernelMessageRights)4U)
#define UMICOM_MESSAGE_RIGHT_DUPLICATE ((UmicomKernelMessageRights)8U)
#define UMICOM_MESSAGE_RIGHT_TRANSFER ((UmicomKernelMessageRights)16U)
#define UMICOM_MESSAGE_RIGHT_ALL ((UmicomKernelMessageRights)31U)

typedef enum UmicomKernelMessageStatus {
    UMICOM_MESSAGE_OK = 0,
    UMICOM_MESSAGE_INVALID_ARGUMENT,
    UMICOM_MESSAGE_NOT_INITIALISED,
    UMICOM_MESSAGE_BAD_STATE,
    UMICOM_MESSAGE_INVALID_HANDLE,
    UMICOM_MESSAGE_WRONG_OWNER,
    UMICOM_MESSAGE_ACCESS_DENIED,
    UMICOM_MESSAGE_CHANNEL_LIMIT,
    UMICOM_MESSAGE_HANDLE_LIMIT,
    UMICOM_MESSAGE_OWNER_LIMIT,
    UMICOM_MESSAGE_TOO_LARGE,
    UMICOM_MESSAGE_WOULD_BLOCK,
    UMICOM_MESSAGE_PEER_CLOSED,
    UMICOM_MESSAGE_BUFFER_TOO_SMALL,
    UMICOM_MESSAGE_SEQUENCE_EXHAUSTED,
    UMICOM_MESSAGE_STATE_CHANGED,
    UMICOM_MESSAGE_BAD_USER_BUFFER,
    UMICOM_MESSAGE_SERVICE_UNBOUND,
    UMICOM_MESSAGE_CORRUPT_STATE
} UmicomKernelMessageStatus;

/* Metadata is assigned by the Kernel. The sender cannot claim a different
 * identity or sequence by placing those numbers in its payload. The receiver
 * treats data[] as bytes; it is not necessarily text or NUL-terminated. */
typedef struct UmicomKernelMessage {
    UmicomU64 sender;
    UmicomU64 sequence;
    UmicomU64 bytes;
    UmicomU8 data[UMICOM_MESSAGE_MAX_BYTES];
} UmicomKernelMessage;
#define UMICOM_MESSAGE_HEADER_BYTES 24U
_Static_assert(__builtin_offsetof(UmicomKernelMessage, data) == UMICOM_MESSAGE_HEADER_BYTES,
    "Message bytes must follow the three fixed-width metadata words");
_Static_assert(sizeof(UmicomKernelMessage) == 280U, "Message wire layout must have no hidden padding");

/* Readiness is a snapshot, not a promise that a concurrent operation will work.
 * The current implementation requires serialised access on one hart. */
#define UMICOM_MESSAGE_READABLE ((UmicomU64)1U)
#define UMICOM_MESSAGE_WRITABLE ((UmicomU64)2U)
#define UMICOM_MESSAGE_PEER_GONE ((UmicomU64)4U)
typedef struct UmicomKernelMessageInfo {
    UmicomU64 rights;
    UmicomU64 queued;
    UmicomU64 nextBytes;
    UmicomU64 flags;
    UmicomU64 endpointReferences;
} UmicomKernelMessageInfo;
_Static_assert(sizeof(UmicomKernelMessageInfo) == 40U, "Message query layout must remain explicit");

/* SEND:    a0=handle, a1=source, a2=payload bytes (1..256).
 * RECEIVE: a0=handle, a1=destination, a2=total buffer capacity. Success writes
 *          24 metadata bytes followed by exactly message.bytes payload bytes.
 * QUERY:   a0=handle, a1=UmicomKernelMessageInfo destination, a2=0.
 * CLOSE:   a0=handle. Closing needs ownership, not an additional right.
 *
 * Empty/full queues return WOULD_BLOCK immediately. A closed peer is distinct
 * from an empty live peer. Existing queued messages can be drained after the
 * sending peer closes; only then does RECEIVE return PEER_CLOSED. */
#endif /* UMICOM_KERNEL_MESSAGE_ABI_H */
