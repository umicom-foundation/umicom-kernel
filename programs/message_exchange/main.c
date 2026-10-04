/*-----------------------------------------------------------------------------
 * Umicom Kernel native message-exchange program
 * File: programs/message_exchange/main.c
 *
 * PURPOSE:
 *   Exercise SEND, RECEIVE, QUERY and CLOSE through real user ECALLs in two
 *   independently loaded instances. Rights supplied by admission select the role.
 *
 * EDUCATIONAL OVERVIEW:
 *   The sender and receiver use the same virtual buffer addresses but own
 *   different physical frames. After SEND succeeds the program overwrites its
 *   source; the receiver must still obtain the original copied bytes. Its
 *   destination crosses a page boundary, exercising the checked-copy service.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/message_abi.h"
#include "umicom/kernel/user_abi.h"

/* Nonempty RO data keeps the executable's three permission classes observable. */
static const volatile UmicomU8 umicomMessageProgramName[16] = "Umicom messages";
/* Align to a page so the boundary crossed below is not a link-layout guess. */
alignas(4096) static volatile UmicomU8 umicomMessageBuffer[8192];
UmicomU64 UmicomMessageProgramCall(UmicomU64 number, UmicomU64 first, UmicomU64 second, UmicomU64 third);

static UmicomU64 UmicomMessageProgramPointer(const volatile void *pointer)
{
    return (UmicomU64)(UmicomUIntPtr)pointer;
}
UmicomU64 UmicomMessageProgramMain(UmicomU64 handle)
{
    if (umicomMessageProgramName[0] != 'U') return 0xea01U;
    const UmicomU64 identity = UmicomMessageProgramCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
    UmicomKernelMessageInfo info;
    /* QUERY writes this record through the user/Kernel boundary. The address
     * helper below only converts a pointer; its const-qualified parameter does
     * not tell the compiler that the later ECALL will fill the object.
     * Give the complete record a defined starting value before taking that
     * address. A zeroed record is not a successful query: the status check
     * below must still pass before returned rights or readiness are used. */
    info = (UmicomKernelMessageInfo) {
        .rights = 0U,             /* Assume no authority before QUERY succeeds. */
        .queued = 0U,             /* Queue depth must come from the Kernel. */
        .nextBytes = 0U,          /* No pending packet size has been observed. */
        .flags = 0U,              /* No readiness condition has been confirmed. */
        .endpointReferences = 0U  /* QUERY supplies the actual reference count. */
    };
    if (identity == 0U || UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_QUERY, handle,
        UmicomMessageProgramPointer(&info), 0U) != UMICOM_MESSAGE_OK) return 0xea02U;

    /* The 32-byte source crosses a mapped page boundary. RECEIVE uses 56 bytes
     * here: three metadata words followed by the same 32-byte payload. */
    volatile UmicomU8 *const crossing = &umicomMessageBuffer[4096U - 16U];
    const UmicomU64 crossingAddress = UmicomMessageProgramPointer(crossing);
    if ((info.rights & UMICOM_MESSAGE_RIGHT_SEND) != 0U) {
        /* A SEND-only endpoint must not acquire RECEIVE by changing call number. */
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, handle,
            crossingAddress, 280U) != UMICOM_MESSAGE_ACCESS_DENIED) return 0xea03U;
        /* Overflowing/unmapped source and excessive length must not enqueue. */
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_SEND, handle,
            ~(UmicomU64)0U - 7U, 32U) != UMICOM_MESSAGE_BAD_USER_BUFFER) return 0xea04U;
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_SEND, handle,
            crossingAddress, UMICOM_MESSAGE_MAX_BYTES + 1U) != UMICOM_MESSAGE_TOO_LARGE) return 0xea05U;
        for (UmicomU64 packet = 0U; packet < UMICOM_MESSAGE_QUEUE_DEPTH; ++packet) {
            for (UmicomSize byte = 0U; byte < 32U; ++byte) crossing[byte] = (UmicomU8)(packet * 32U + byte);
            if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_SEND, handle,
                crossingAddress, 32U) != UMICOM_MESSAGE_OK) return 0xea06U;
            /* The queue may not borrow this buffer after the call returns. */
            for (UmicomSize byte = 0U; byte < 32U; ++byte) crossing[byte] = 0xeeU;
        }
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_SEND, handle,
            crossingAddress, 32U) != UMICOM_MESSAGE_WOULD_BLOCK) return 0xea07U;
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_CLOSE, handle, 0U, 0U) != UMICOM_MESSAGE_OK) return 0xea08U;
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_QUERY, handle,
            UmicomMessageProgramPointer(&info), 0U) != UMICOM_MESSAGE_INVALID_HANDLE) return 0xea09U;
        return 0x5100U + identity;
    }
    /* The producer closed before this invocation. Readability and peer closure
     * coexist while the receiver still has queued packets to drain. */
    if (info.queued != UMICOM_MESSAGE_QUEUE_DEPTH ||
        (info.flags & (UMICOM_MESSAGE_READABLE | UMICOM_MESSAGE_PEER_GONE)) !=
        (UMICOM_MESSAGE_READABLE | UMICOM_MESSAGE_PEER_GONE)) return 0xea10U;
    if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_SEND, handle,
        crossingAddress, 1U) != UMICOM_MESSAGE_ACCESS_DENIED) return 0xea11U;
    if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, handle,
        crossingAddress, UMICOM_MESSAGE_HEADER_BYTES) != UMICOM_MESSAGE_BUFFER_TOO_SMALL) return 0xea12U;
    if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, handle,
        ~(UmicomU64)0U - 7U, 280U) != UMICOM_MESSAGE_BAD_USER_BUFFER) return 0xea13U;
    UmicomU64 sender = 0U;
    for (UmicomU64 packet = 0U; packet < UMICOM_MESSAGE_QUEUE_DEPTH; ++packet) {
        if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, handle,
            crossingAddress, 280U) != UMICOM_MESSAGE_OK) return 0xea14U;
        /* The buffer has eight-byte alignment. The native ABI is RV64 little
         * endian, and the layout assertions keep these three words stable. */
        const volatile UmicomU64 *const header = (const volatile UmicomU64 *)crossing;
        if (header[0] == 0U || header[0] == identity || header[1] != packet + 1U || header[2] != 32U) return 0xea15U;
        if (packet == 0U) sender = header[0];
        if (header[0] != sender) return 0xea16U;
        for (UmicomSize byte = 0U; byte < 32U; ++byte) {
            if (crossing[UMICOM_MESSAGE_HEADER_BYTES + byte] != (UmicomU8)(packet * 32U + byte)) return 0xea17U;
        }
    }
    if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE, handle,
        crossingAddress, 280U) != UMICOM_MESSAGE_PEER_CLOSED) return 0xea18U;
    if (UmicomMessageProgramCall(UMICOM_USER_CALL_MESSAGE_CLOSE, handle, 0U, 0U) != UMICOM_MESSAGE_OK) return 0xea19U;
    return 0x5200U + sender * 16U + identity;
}
