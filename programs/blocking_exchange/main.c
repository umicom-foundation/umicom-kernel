/*-----------------------------------------------------------------------------
 * Umicom Kernel native blocking-message program
 * File: programs/blocking_exchange/main.c
 *
 * PURPOSE:
 *   Exchange more messages than the queue holds while retaining user stacks
 *   across blocked calls. Different rights select producer and consumer roles.
 *
 * EDUCATIONAL NOTE:
 *   No retry loop polls an empty or full queue. A waiting ECALL returns only
 *   after Kernel completion; this same C invocation then checks its locals and
 *   continues. A start counter detects restarting at the ELF entry by mistake.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/blocking_ipc_abi.h"
#include "umicom/kernel/user_abi.h"
#include "umicom/kernel/ipc_workload.h"

static const volatile UmicomU8 umicomIpcProgramName[16] = "Umicom IPC";
__attribute__((section(".data.umicom_ipc_observation")))
static volatile UmicomKernelIpcObservation umicomIpcObservation;
alignas(4096) static volatile UmicomU8 umicomIpcBuffer[8192];
UmicomU64 UmicomIpcProgramCall(UmicomU64 number, UmicomU64 a, UmicomU64 b, UmicomU64 c, UmicomU64 d);
static UmicomU64 UmicomIpcAddress(const volatile void *pointer)
{
    return (UmicomU64)(UmicomUIntPtr)pointer;
}
static UmicomU64 UmicomIpcWord(const volatile UmicomU8 *bytes)
{
    /* Decode wire bytes without treating a byte array as a different C type. */
    UmicomU64 value = 0U;
    for (UmicomSize index = 0U; index < 8U; ++index) value |= (UmicomU64)bytes[index] << (index * 8U);
    return value;
}
__attribute__((noinline)) static UmicomU64 UmicomIpcExchange(UmicomU64 handle, UmicomU64 identity, UmicomU64 rights)
{
    /* Volatile locals force stack traffic. They must survive both timer pauses
     * and a wait that lets the peer run on an independent address space. */
    volatile UmicomU64 locals[4] = { 0x111U, 0x222U, 0x333U, 0x444U };
    volatile UmicomU8 *const buffer = &umicomIpcBuffer[4096U - 16U];
    const UmicomU64 address = UmicomIpcAddress(buffer);
    UmicomU64 sender = 0U;
    for (UmicomU64 packet = 0U; packet < UMICOM_IPC_PACKET_COUNT; ++packet) {
        if ((rights & UMICOM_MESSAGE_RIGHT_SEND) != 0U) {
            for (UmicomSize byte = 0U; byte < UMICOM_IPC_PACKET_BYTES; ++byte)
                buffer[byte] = (UmicomU8)(packet * UMICOM_IPC_PACKET_BYTES + byte);
            if (UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_SEND_WAIT, handle, address,
                UMICOM_IPC_PACKET_BYTES, UMICOM_IPC_WAIT_FOREVER) != UMICOM_MESSAGE_OK) return 0xeb01U;
            /* Accepted packets own copies, not this mutable source. */
            for (UmicomSize byte = 0U; byte < UMICOM_IPC_PACKET_BYTES; ++byte) buffer[byte] = 0xeeU;
        } else {
            if (UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, handle, address,
                sizeof(UmicomKernelMessage), UMICOM_IPC_WAIT_FOREVER) != UMICOM_MESSAGE_OK) return 0xeb02U;
            const UmicomU64 observedSender = UmicomIpcWord(buffer);
            if (observedSender == 0U || observedSender == identity ||
                UmicomIpcWord(buffer + 8U) != packet + 1U ||
                UmicomIpcWord(buffer + 16U) != UMICOM_IPC_PACKET_BYTES) return 0xeb03U;
            if (packet == 0U) sender = observedSender;
            if (observedSender != sender) return 0xeb04U;
            for (UmicomSize byte = 0U; byte < UMICOM_IPC_PACKET_BYTES; ++byte)
                if (buffer[UMICOM_MESSAGE_HEADER_BYTES + byte] != (UmicomU8)(packet * UMICOM_IPC_PACKET_BYTES + byte))
                    return 0xeb05U;
        }
        if (locals[0] != 0x111U || locals[1] != 0x222U || locals[2] != 0x333U || locals[3] != 0x444U)
            return 0xeb06U;
        umicomIpcObservation.stackProof = locals[0] + locals[1] + locals[2] + locals[3];
        ++umicomIpcObservation.packets;
    }
    if ((rights & UMICOM_MESSAGE_RIGHT_SEND) == 0U &&
        UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, handle, address,
            sizeof(UmicomKernelMessage), UMICOM_IPC_WAIT_FOREVER) != UMICOM_MESSAGE_PEER_CLOSED) return 0xeb07U;
    /* Deliberately do not CLOSE. The attached scheduler's terminal cleanup must
     * release this owner's endpoints while preserving accepted peer messages. */
    return (rights & UMICOM_MESSAGE_RIGHT_SEND) != 0U ? 0x7101U : 0x7102U;
}
UmicomU64 UmicomIpcProgramMain(UmicomU64 handle)
{
    if (umicomIpcProgramName[0] != 'U') return 0xeb10U;
    ++umicomIpcObservation.starts;
    if (umicomIpcObservation.starts != 1U) return 0xeb11U;
    const UmicomU64 mode = umicomIpcObservation.mode;
    if (mode == UMICOM_IPC_MODE_ENDLESS) for (;;) __asm__ volatile("nop");
    if (mode == UMICOM_IPC_MODE_FAULT) { __asm__ volatile("unimp"); return 0xeb12U; }
    const UmicomU64 buffer = UmicomIpcAddress(&umicomIpcBuffer[4096U - 16U]);
    if (mode == UMICOM_IPC_MODE_TIMEOUT || mode == UMICOM_IPC_MODE_PEER_CLOSE) {
        const UmicomU64 result = UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT,
            handle, buffer, sizeof(UmicomKernelMessage),
            mode == UMICOM_IPC_MODE_TIMEOUT ? 100000U : UMICOM_IPC_WAIT_FOREVER);
        return mode == UMICOM_IPC_MODE_TIMEOUT ?
            (result == UMICOM_IPC_RESULT_TIMED_OUT ? 0x7200U : 0xeb13U) :
            (result == UMICOM_MESSAGE_PEER_CLOSED ? 0x7300U : 0xeb14U);
    }
    if (mode == UMICOM_IPC_MODE_REFUSAL) {
        if (UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, handle, ~(UmicomU64)0U - 7U,
            sizeof(UmicomKernelMessage), UMICOM_IPC_WAIT_FOREVER) != UMICOM_MESSAGE_BAD_USER_BUFFER) return 0xeb15U;
        if (UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, handle, buffer,
            sizeof(UmicomKernelMessage), 0U) != UMICOM_MESSAGE_WOULD_BLOCK) return 0xeb16U;
        return 0x7400U;
    }
    /* Define the whole output before its address crosses the ECALL boundary.
     * Zero values do not replace the status check or grant any authority. */
    UmicomKernelMessageInfo info = { .rights = 0U, .queued = 0U, .nextBytes = 0U, .flags = 0U, .endpointReferences = 0U };
    const UmicomU64 identity = UmicomIpcProgramCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U, 0U);
    if (identity == 0U || UmicomIpcProgramCall(UMICOM_USER_CALL_MESSAGE_QUERY, handle,
        UmicomIpcAddress(&info), 0U, 0U) != UMICOM_MESSAGE_OK) return 0xeb17U;
    return UmicomIpcExchange(handle, identity, info.rights);
}
