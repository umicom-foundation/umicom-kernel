/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/device_tree.c
 *
 * PURPOSE:
 *   Inspect enough of the standard Flattened Device Tree header to protect the
 *   QEMU-provided DTB from the physical-memory foundation's physical frame allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   Device-tree integers are stored in big-endian byte order regardless of the
 *   CPU's native byte order.  RV64 QEMU is little-endian, so the four bytes of
 *   each 32-bit field are assembled explicitly rather than dereferenced as a
 *   native `UmicomU32`.
 *
 *   the physical-memory foundation intentionally validates address ranges before dereferencing the DTB.
 *   The future trap/page-fault subsystem must not be used as a substitute for
 *   ordinary input validation.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import checked address addition used before reading physical memory. */
#include "umicom/kernel/address.h"

/* Import this module's public FDT inspection contract. */
#include "umicom/kernel/device_tree.h"

/* Convert four big-endian bytes into one host integer without assuming native
 * alignment or native byte order. */
static UmicomU32 ReadBigEndianU32(const UmicomU8 *bytes)
{
    /* Move the most-significant byte into bits 31..24. */
    const UmicomU32 byte0 = (UmicomU32)bytes[0] << 24U;

    /* Move the next byte into bits 23..16. */
    const UmicomU32 byte1 = (UmicomU32)bytes[1] << 16U;

    /* Move the next byte into bits 15..8. */
    const UmicomU32 byte2 = (UmicomU32)bytes[2] << 8U;

    /* The final byte already belongs in bits 7..0. */
    const UmicomU32 byte3 = (UmicomU32)bytes[3];

    /* Combining the four non-overlapping fields reconstructs the value stored
     * by the big-endian device-tree format. */
    return byte0 | byte1 | byte2 | byte3;
}

UmicomBoolean UmicomKernelDeviceTreeInspect(
    UmicomAddress deviceTreeAddress,
    UmicomAddress ramBase,
    UmicomSize ramBytes,
    UmicomKernelDeviceTreeInfo *outInfo
)
{
    /* The caller must provide storage for validated information. */
    if (outInfo == (UmicomKernelDeviceTreeInfo *)0) {
        /* Refuse to inspect a range that cannot be published safely. */
        return UMICOM_FALSE;
    }

    /* A zero-length RAM description cannot contain a valid DTB. */
    if (ramBytes == (UmicomSize)0U) {
        /* Reject the impossible memory map before doing address arithmetic. */
        return UMICOM_FALSE;
    }

    /* Calculate the exclusive end of RAM using overflow-checked arithmetic. */
    UmicomAddress ramEnd = (UmicomAddress)0U;

    /* Refuse a RAM description whose base+length wraps the address space. */
    if (
        UmicomKernelAddressAddChecked(ramBase, ramBytes, &ramEnd) ==
        UMICOM_FALSE
    ) {
        /* The memory description itself is not trustworthy. */
        return UMICOM_FALSE;
    }

    /* The DTB must begin at or above the first RAM byte. */
    if (deviceTreeAddress < ramBase) {
        /* Reading below RAM could touch ROM/MMIO or unmapped space. */
        return UMICOM_FALSE;
    }

    /* Prove that the first eight bytes required for magic+totalsize are inside
     * RAM before any pointer dereference occurs. */
    UmicomAddress firstFieldsEnd = (UmicomAddress)0U;

    /* Calculate the exclusive address after those first eight bytes. */
    if (
        UmicomKernelAddressAddChecked(
            deviceTreeAddress,
            (UmicomSize)8U,
            &firstFieldsEnd
        ) == UMICOM_FALSE
    ) {
        /* Addition overflow means the supplied pointer cannot be trusted. */
        return UMICOM_FALSE;
    }

    /* The first header fields must not extend beyond physical RAM. */
    if (firstFieldsEnd > ramEnd) {
        /* Do not dereference a truncated/out-of-range header. */
        return UMICOM_FALSE;
    }

    /* Convert the validated physical address to a byte pointer.
     *
     * the physical-memory foundation executes with physical addressing and no page tables, so this direct
     * pointer denotes the same RAM byte. */
    const UmicomU8 *const header =
        (const UmicomU8 *)(UmicomUIntPtr)deviceTreeAddress;

    /* Read the standard 32-bit big-endian FDT magic at header offset 0. */
    const UmicomU32 magic = ReadBigEndianU32(header);

    /* Reject any blob that is not a standard flattened device tree. */
    if (magic != UMICOM_KERNEL_FDT_MAGIC) {
        /* the physical-memory foundation must not reserve a guessed size from an unrecognised structure. */
        return UMICOM_FALSE;
    }

    /* Read the standard big-endian totalsize field at header offset 4. */
    const UmicomU32 totalBytes32 = ReadBigEndianU32(header + 4U);

    /* Widen the 32-bit format field into the Kernel's normal size type. */
    const UmicomSize totalBytes = (UmicomSize)totalBytes32;

    /* A valid tree must be large enough to contain the complete fixed header. */
    if (totalBytes < UMICOM_KERNEL_FDT_MINIMUM_HEADER_BYTES) {
        /* Reject a corrupt/truncated blob before range reservation. */
        return UMICOM_FALSE;
    }

    /* Prove the complete blob's exclusive end without unsigned wrap. */
    UmicomAddress deviceTreeEnd = (UmicomAddress)0U;

    /* Calculate address + totalBytes using the common checked helper. */
    if (
        UmicomKernelAddressAddChecked(
            deviceTreeAddress,
            totalBytes,
            &deviceTreeEnd
        ) == UMICOM_FALSE
    ) {
        /* A wrapping total size is invalid input. */
        return UMICOM_FALSE;
    }

    /* The complete DTB must remain within the physical RAM declared by the
     * selected the physical-memory foundation platform profile. */
    if (deviceTreeEnd > ramEnd) {
        /* Do not let a corrupt totalsize reserve or read outside RAM. */
        return UMICOM_FALSE;
    }

    /* Publish the validated physical start address. */
    outInfo->address = deviceTreeAddress;

    /* Publish the validated complete byte count. */
    outInfo->totalBytes = totalBytes;

    /* The caller may now safely reserve the containing physical pages. */
    return UMICOM_TRUE;
}
