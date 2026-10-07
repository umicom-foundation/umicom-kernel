/*-----------------------------------------------------------------------------
 * Umicom Kernel disk parser/transport integration tests.
 *
 * Reuse the existing register and DMA model verbatim, changing only its source
 * of disk bytes to the new fixture. The actual driver, reset lifecycle, parser,
 * console adapter and guest C acceptance sequence are linked below. This does
 * not execute RISC-V instructions, QEMU or physical hardware.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/types.h"
#include "fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>
static UmicomU8 *umicomTransportImage;
/* The existing fixture helper is intentionally substituted only in this host
 * model compilation. The original model file and raw-block tests are unchanged. */
#define UMICOM_BLOCK_FIXTURE_FORMAT_H
#define UMICOM_BLOCK_FIXTURE_SECTORS 128U
static UmicomU8 UmicomBlockFixtureByte(UmicomU64 sector, UmicomSize offset)
{
    if (sector >= UMICOM_DISK_FIXTURE_SECTORS || offset >= 512U) abort();
    return umicomTransportImage[sector * 512U + offset];
}
#define main UmicomBlockModelRegressionEntry
#include "../virtio_block/virtio_block_tests.c"
#undef main
#include "umicom/kernel/disk_console.h"

UmicomU64 UmicomPlatformTimerRead(void) { return model.now++; }
static UmicomKernelFat16 umicomTransportVolume;
static UmicomKernelBlockHandle umicomTransportHandle;
static UmicomBoolean SectorRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    CHECK(context == &domain);
    return UmicomKernelBlockRead(&domain, umicomTransportHandle, sector, 1U, output, 512U) == UMICOM_BLOCK_OK ?
        UMICOM_TRUE : UMICOM_FALSE;
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const size_t size = (size_t)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    umicomTransportImage = malloc(size); CHECK(umicomTransportImage);
    FILE *file = fopen(argv[2], "rb"); CHECK(file);
    CHECK(fread(umicomTransportImage, 1U, size, file) == size && fgetc(file) == EOF); CHECK(fclose(file) == 0);
    Start(); model.capacity = UMICOM_DISK_FIXTURE_SECTORS;
    if (!strcmp(argv[1], "guest_sequence")) {
        UmicomKernelDiskInspectionValidate();
        CHECK(strstr(transcript, "UMICOM_KERNEL_DISK_INSPECTION_READY") != 0 && Allocated() == 0U);
    } else if (!strcmp(argv[1], "driver_fragmented")) {
        umicomTransportHandle = Open();
        const UmicomKernelDiskReader reader = {model.capacity, SectorRead, &domain};
        CHECK(UmicomKernelFat16Open(&umicomTransportVolume, &reader, 0U) == UMICOM_DISK_OK);
        UmicomSize count = 0U;
        CHECK(UmicomKernelFat16Read(&umicomTransportVolume, "/FRAG.BIN", 0U, resultBytes, sizeof(resultBytes), &count) == UMICOM_DISK_OK && count == 1300U);
        for (UmicomSize i = 0U; i < count; ++i) CHECK(resultBytes[i] == UmicomDiskFixturePattern(i));
        CHECK(UmicomKernelFat16Close(&umicomTransportVolume) == UMICOM_DISK_OK); Close(umicomTransportHandle);
    } else if (!strcmp(argv[1], "console_partitions")) {
        CHECK(UmicomKernelDiskInspect(0U, 0U, "partitions", "/", 0, Output) == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "first-sector=2048 sectors=12288") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_info")) {
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", 0, Output) == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "FAT16 label=UMICOMDISK clusters=12159") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_list")) {
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatls", "/DOCS", 0, Output) == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "GUIDE.TXT") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_cat")) {
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatcat", "/README.TXT", 0, Output) == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "No disk writes or repairs are enabled.") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_binary_escape")) {
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatcat", "/FRAG.BIN", 0, Output) == UMICOM_DISK_OK);
        CHECK(strstr(transcript, "\\x1b") && !strchr(transcript, 27) && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_bad_media")) {
        umicomTransportImage[510] = 0U;
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", 0, Output) == UMICOM_DISK_SIGNATURE);
        CHECK(strstr(transcript, "disk.inspect=bad-signature") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_bad_completion")) {
        model.usedId = 1U;
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", 0, Output) == UMICOM_DISK_IO_ERROR);
        CHECK(strstr(transcript, "malformed-completion") && Allocated() == 0U);
    } else if (!strcmp(argv[1], "console_close_retry")) {
        model.stuckReset = UMICOM_TRUE;
        /* Failed initial reset still retains the driver handle; the adapter
         * cannot overwrite it while a device might keep ownership. */
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", 0, Output) == UMICOM_DISK_IO_ERROR);
        CHECK(UmicomKernelDiskInspectionClose() == UMICOM_BLOCK_RESET_PENDING);
        model.stuckReset = UMICOM_FALSE;
        CHECK(UmicomKernelDiskInspectionClose() == UMICOM_BLOCK_OK && Allocated() == 0U);
        CHECK(UmicomKernelDiskInspect(0U, 0U, "fatinfo", "/", 0, Output) == UMICOM_DISK_OK);
    } else if (!strcmp(argv[1], "driver_io_unchanged")) {
        umicomTransportHandle = Open();
        const UmicomKernelDiskReader reader = {model.capacity, SectorRead, &domain};
        CHECK(UmicomKernelFat16Open(&umicomTransportVolume, &reader, 0U) == UMICOM_DISK_OK);
        model.result = 1U; memset(resultBytes, 0xa5, sizeof(resultBytes)); UmicomSize count = 99U;
        CHECK(UmicomKernelFat16Read(&umicomTransportVolume, "/FRAG.BIN", 0U, resultBytes, sizeof(resultBytes), &count) == UMICOM_DISK_IO_ERROR && count == 99U);
        Unchanged(); model.result = 0U;
        CHECK(UmicomKernelFat16Close(&umicomTransportVolume) == UMICOM_DISK_OK); Close(umicomTransportHandle);
    } else if (!strcmp(argv[1], "console_commands")) {
        UmicomKernelConsoleShell shell = {0}; shell.output = Output;
        UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
        CHECK(UmicomKernelShellParse("fatls 0 0 /DOCS", 15U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelDiskInspectionCommand(&shell, &command, &handled) == UMICOM_SHELL_OK && handled);
        CHECK(UmicomKernelShellParse("fatls 0", 7U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelDiskInspectionCommand(&shell, &command, &handled) == UMICOM_SHELL_INVALID_ARGUMENT && handled);
    } else { fprintf(stderr, "unknown integration case\n"); return 2; }
    CHECK(model.observedReadOnlyRequest || model.notifications == 0U);
    free(umicomTransportImage); return 0;
}
