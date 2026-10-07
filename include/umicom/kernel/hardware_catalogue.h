/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/hardware_catalogue.h
 *
 * PURPOSE:
 *   Turn an indexed firmware tree into copied, read-only hardware observations.
 *
 * A catalogue is not a driver and a register span is not permission to access
 * MMIO. In particular, a virtio-mmio node advertises a transport slot; only a
 * later driver can establish whether a device is attached and which features
 * it offers. This layer never reads that slot or changes allocator ownership.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_HARDWARE_CATALOGUE_H
#define UMICOM_KERNEL_HARDWARE_CATALOGUE_H
#include "umicom/kernel/device_tree_reader.h"

#define UMICOM_HARDWARE_DEVICE_LIMIT 64U
#define UMICOM_HARDWARE_REG_LIMIT 4U
#define UMICOM_HARDWARE_COMPATIBLE_BYTES 96U
#define UMICOM_HARDWARE_RANGES_LIMIT 32U

typedef enum UmicomKernelHardwareKind {
    UMICOM_HARDWARE_OTHER, UMICOM_HARDWARE_MEMORY, UMICOM_HARDWARE_CPU,
    UMICOM_HARDWARE_UART, UMICOM_HARDWARE_TIMER, UMICOM_HARDWARE_INTERRUPT_CONTROLLER,
    UMICOM_HARDWARE_VIRTIO_MMIO, UMICOM_HARDWARE_PCI_HOST, UMICOM_HARDWARE_RESERVED_MEMORY
} UmicomKernelHardwareKind;
typedef struct UmicomKernelHardwareRegister {
    UmicomU64 busAddress;      /* Encoded in the immediate parent's address space. */
    UmicomU64 bytes;
    UmicomU64 physicalAddress; /* Meaningful only when translation == OK, except CPU IDs. */
    UmicomKernelTreeStatus translation;
} UmicomKernelHardwareRegister;
typedef struct UmicomKernelHardwareDevice {
    char path[UMICOM_TREE_PATH_BYTES];
    char compatible[UMICOM_HARDWARE_COMPATIBLE_BYTES]; /* First string, not the whole list. */
    UmicomKernelHardwareKind kind;
    UmicomBoolean enabled; /* A disabled ancestor disables this inventory entry too. */
    UmicomU32 phandle;
    UmicomU32 interruptParent; /* Raw inherited reference; no IRQ route is promised. */
    UmicomBoolean hasInterruptSpecifiers;
    UmicomKernelTreeStatus registerStatus;
    UmicomU32 registerCount;
    UmicomKernelHardwareRegister registers[UMICOM_HARDWARE_REG_LIMIT];
} UmicomKernelHardwareDevice;
typedef struct UmicomKernelHardwareCatalogue {
    UmicomBoolean ready; /* Only true after the complete supported inventory is copied. */
    UmicomU32 nodes;
    UmicomU32 properties;
    UmicomU32 devices;
    UmicomU32 firmwareReservations;
    UmicomU64 timebaseFrequency; /* Zero means absent; not a guessed clock rate. */
    char model[UMICOM_HARDWARE_COMPATIBLE_BYTES];
    char stdoutPath[UMICOM_TREE_PATH_BYTES]; /* Literal chosen value; aliases are not resolved. */
    UmicomKernelTreeReservation reservations[UMICOM_TREE_RESERVATION_LIMIT];
    UmicomKernelHardwareDevice entries[UMICOM_HARDWARE_DEVICE_LIMIT];
} UmicomKernelHardwareCatalogue;

/* The reader and its bytes must remain immutable during Build. The completed
 * catalogue owns all its values and strings; it no longer borrows the DTB.
 * Unknown bus encodings remain visibly untranslated. A malformed supported
 * encoding or a capacity overflow fails the whole catalogue, never truncates it.
 * Input/output owners must be trusted, separate storage. */
UmicomKernelTreeStatus UmicomKernelHardwareCatalogueBuild(
    const UmicomKernelDeviceTreeReader *reader, UmicomKernelHardwareCatalogue *catalogue);
const char *UmicomKernelHardwareKindName(UmicomKernelHardwareKind kind);

/* Boot capture checks the already-selected platform RAM bounds before reading
 * firmware bytes. It is observational: drivers, RAM geometry and reservations
 * are not reconfigured from the result. Recovery does not call this path. */
void UmicomKernelHardwareCapture(UmicomAddress deviceTreeAddress);
UmicomKernelTreeStatus UmicomKernelHardwareCaptureStatus(void);
const UmicomKernelHardwareCatalogue *UmicomKernelHardwareCatalogueRead(void);
typedef void (*UmicomKernelHardwareWrite)(void *context, const char *text, UmicomSize bytes);
void UmicomKernelHardwareReport(void *context, UmicomKernelHardwareWrite write);
void UmicomKernelHardwareValidateExecution(void);
#endif /* UMICOM_KERNEL_HARDWARE_CATALOGUE_H */
