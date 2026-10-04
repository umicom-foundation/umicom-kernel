/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/message_service.c
 *
 * PURPOSE:
 *   Turn register-based message requests into owned queue operations, using the
 *   established page walker and user-memory checks for every byte transferred.
 *
 * EDUCATIONAL OVERVIEW:
 *   A user address is not a machine-mode pointer. Read and write the complete
 *   span only after checking its mappings, user permission and backing records.
 *   A receive leaves the packet queued until delivery succeeds. An invalid
 *   second destination page must not consume a message or alter the first page.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/message_service.h"

/* Only Kernel admission writes this binding. There is one running hart and no
 * concurrent mapper or queue consumer during a service call. No user argument
 * can select another domain or replace the identity checked below. */
static UmicomKernelMessageDomain *umicomMessageBoundDomain;
static UmicomU64 umicomMessageBoundIdentity;

UmicomKernelMessageStatus UmicomKernelMessageServiceBind(
    UmicomKernelMessageDomain *domain, UmicomU64 processIdentity)
{
    if (processIdentity == 0U) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    if (umicomMessageBoundDomain != (UmicomKernelMessageDomain *)0) return UMICOM_MESSAGE_BAD_STATE;
    const UmicomKernelMessageStatus status = UmicomKernelMessageValidate(domain);
    if (status != UMICOM_MESSAGE_OK) return status;
    umicomMessageBoundDomain = domain;
    umicomMessageBoundIdentity = processIdentity;
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageServiceUnbind(
    UmicomKernelMessageDomain *domain, UmicomU64 processIdentity)
{
    /* A mismatched caller must not tear down another invocation's binding. */
    if (domain == (UmicomKernelMessageDomain *)0 || processIdentity == 0U ||
        umicomMessageBoundDomain != domain || umicomMessageBoundIdentity != processIdentity) {
        return UMICOM_MESSAGE_BAD_STATE;
    }
    umicomMessageBoundDomain = (UmicomKernelMessageDomain *)0;
    umicomMessageBoundIdentity = 0U;
    return UMICOM_MESSAGE_OK;
}
UmicomBoolean UmicomKernelMessageServiceRecognizes(UmicomU64 number)
{
    return number >= UMICOM_USER_CALL_MESSAGE_SEND && number <= UMICOM_USER_CALL_MESSAGE_CLOSE
        ? UMICOM_TRUE : UMICOM_FALSE;
}

/* Existing checked copies are deliberately limited to 64 bytes each. Preflight
 * the larger bounded message span once, then reuse those copies in small chunks.
 * This keeps the original COPY service and its public limit unchanged. */
static UmicomKernelMessageStatus UmicomMessageReadUser(
    const UmicomKernelUserMemory *memory, UmicomAddress source, UmicomU8 *output, UmicomSize bytes)
{
    if (UmicomKernelUserMemoryCheck(memory, source, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_READ) != UMICOM_USER_RESULT_OK) {
        return UMICOM_MESSAGE_BAD_USER_BUFFER;
    }
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize remaining = bytes - offset;
        const UmicomSize chunk = remaining < UMICOM_USER_COPY_LIMIT ? remaining : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryRead(memory, source + offset, output + offset, chunk) != UMICOM_USER_RESULT_OK) {
            return UMICOM_MESSAGE_BAD_USER_BUFFER;
        }
        offset += chunk;
    }
    return UMICOM_MESSAGE_OK;
}
static UmicomKernelMessageStatus UmicomMessageWriteUser(
    const UmicomKernelUserMemory *memory, UmicomAddress destination, const UmicomU8 *input, UmicomSize bytes)
{
    /* Nothing is written before this whole-span check. Page-table ownership is
     * immutable during the following chunks under the single-hart contract. */
    if (UmicomKernelUserMemoryCheck(memory, destination, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != UMICOM_USER_RESULT_OK) {
        return UMICOM_MESSAGE_BAD_USER_BUFFER;
    }
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize remaining = bytes - offset;
        const UmicomSize chunk = remaining < UMICOM_USER_COPY_LIMIT ? remaining : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryWrite(memory, destination + offset, input + offset, chunk) != UMICOM_USER_RESULT_OK) {
            return UMICOM_MESSAGE_BAD_USER_BUFFER;
        }
        offset += chunk;
    }
    return UMICOM_MESSAGE_OK;
}
UmicomKernelMessageStatus UmicomKernelMessageServiceDispatch(
    UmicomKernelUserSession *session, const UmicomRiscvTrapFrame *frame)
{
    if (session == (UmicomKernelUserSession *)0 || frame == (const UmicomRiscvTrapFrame *)0) {
        return UMICOM_MESSAGE_INVALID_ARGUMENT;
    }
    if (umicomMessageBoundDomain == (UmicomKernelMessageDomain *)0 ||
        session->identity == 0U || session->identity != umicomMessageBoundIdentity) {
        return UMICOM_MESSAGE_SERVICE_UNBOUND;
    }
    /* The authenticated sender/receiver comes from Kernel session state. The
     * three user arguments are only a token, a virtual buffer and a byte count. */
    UmicomKernelMessageDomain *const domain = umicomMessageBoundDomain;
    const UmicomU64 owner = session->identity;
    const UmicomKernelMessageHandle handle = frame->x10_a0;
    const UmicomAddress address = (UmicomAddress)frame->x11_a1;
    const UmicomSize bytes = frame->x12_a2;
    if (frame->x17_a7 == UMICOM_USER_CALL_MESSAGE_SEND) {
        if (bytes == 0U) return UMICOM_MESSAGE_INVALID_ARGUMENT;
        if (bytes > UMICOM_MESSAGE_MAX_BYTES) return UMICOM_MESSAGE_TOO_LARGE;
        /* Copy first into private Kernel storage. The channel owns a second
         * copy only after every validation has passed and enqueue commits. */
        UmicomU8 scratch[UMICOM_MESSAGE_MAX_BYTES];
        const UmicomKernelMessageStatus copied = UmicomMessageReadUser(&session->memory, address, scratch, bytes);
        if (copied != UMICOM_MESSAGE_OK) return copied;
        return UmicomKernelMessageSend(domain, owner, handle, scratch, bytes);
    }
    if (frame->x17_a7 == UMICOM_USER_CALL_MESSAGE_RECEIVE) {
        UmicomKernelMessage message;
        UmicomKernelMessageStatus status = UmicomKernelMessagePeek(domain, owner, handle, &message);
        if (status != UMICOM_MESSAGE_OK) return status;
        const UmicomSize deliveredBytes = UMICOM_MESSAGE_HEADER_BYTES + message.bytes;
        if (bytes < deliveredBytes) return UMICOM_MESSAGE_BUFFER_TOO_SMALL;
        status = UmicomMessageWriteUser(&session->memory, address, (const UmicomU8 *)&message, deliveredBytes);
        if (status != UMICOM_MESSAGE_OK) return status;
        /* No mapper, callback or other consumer runs between peek and consume.
         * Checking the exact sequence also refuses accidental stale commits. */
        return UmicomKernelMessageConsume(domain, owner, handle, message.sequence);
    }
    if (frame->x17_a7 == UMICOM_USER_CALL_MESSAGE_QUERY) {
        if (bytes != 0U) return UMICOM_MESSAGE_INVALID_ARGUMENT;
        UmicomKernelMessageInfo info;
        const UmicomKernelMessageStatus status = UmicomKernelMessageQuery(domain, owner, handle, &info);
        if (status != UMICOM_MESSAGE_OK) return status;
        return UmicomMessageWriteUser(&session->memory, address, (const UmicomU8 *)&info, sizeof(info));
    }
    if (frame->x17_a7 == UMICOM_USER_CALL_MESSAGE_CLOSE) {
        return UmicomKernelMessageClose(domain, owner, handle);
    }
    return UMICOM_MESSAGE_INVALID_ARGUMENT;
}
