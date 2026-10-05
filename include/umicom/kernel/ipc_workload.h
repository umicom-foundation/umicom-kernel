/* Umicom Kernel blocking-message acceptance layout. The Kernel writes only
 * admission controls; the separately loaded program owns observations after
 * entry. This diagnostic layout is not a public process API.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_KERNEL_IPC_WORKLOAD_H
#define UMICOM_KERNEL_IPC_WORKLOAD_H
#include "umicom/kernel/types.h"
#define UMICOM_IPC_OBSERVATION_ADDRESS ((UmicomAddress)0x00600000U)
#define UMICOM_IPC_MODE_EXCHANGE 0U
#define UMICOM_IPC_MODE_TIMEOUT 1U
#define UMICOM_IPC_MODE_PEER_CLOSE 2U
#define UMICOM_IPC_MODE_ENDLESS 3U
#define UMICOM_IPC_MODE_FAULT 4U
#define UMICOM_IPC_MODE_REFUSAL 5U
#define UMICOM_IPC_PACKET_COUNT 12U
#define UMICOM_IPC_PACKET_BYTES 32U
typedef struct UmicomKernelIpcObservation {
    UmicomU64 mode;
    UmicomU64 starts;
    UmicomU64 packets;
    UmicomU64 stackProof;
} UmicomKernelIpcObservation;
_Static_assert(sizeof(UmicomKernelIpcObservation) == 32U, "Diagnostic observation has four words");
#endif /* UMICOM_KERNEL_IPC_WORKLOAD_H */
