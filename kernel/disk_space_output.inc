/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_space_output.inc
 *
 * PURPOSE:
 *   Present allocation counts without implying recovery or write permission.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
static void UmicomDiskSpaceOutput(void *context, UmicomKernelBlockOutput output,
    const UmicomKernelFat16Space *space)
{
    UmicomDiskText(context, output, "disk.space.total-clusters=");
    UmicomDiskNumber(context, output, space->totalClusters);
    UmicomDiskText(context, output, " free-clusters=");
    UmicomDiskNumber(context, output, space->freeClusters);
    UmicomDiskText(context, output, " allocated-clusters=");
    UmicomDiskNumber(context, output, space->allocatedClusters);
    UmicomDiskText(context, output, " bad-clusters=");
    UmicomDiskNumber(context, output, space->badClusters);
    UmicomDiskText(context, output, "\r\ndisk.space.cluster-bytes=");
    UmicomDiskNumber(context, output, space->clusterBytes);
    UmicomDiskText(context, output, " data-bytes=");
    UmicomDiskNumber(context, output, space->dataBytes);
    UmicomDiskText(context, output, " free-bytes=");
    UmicomDiskNumber(context, output, space->freeBytes);
    UmicomDiskText(context, output, " allocated-bytes=");
    UmicomDiskNumber(context, output, space->allocatedBytes);
    UmicomDiskText(context, output, " bad-bytes=");
    UmicomDiskNumber(context, output, space->badBytes);
    UmicomDiskText(context, output,
        "\r\nAllocation counts only; not an ownership audit, reservation or repair approval.\r\n");
}
