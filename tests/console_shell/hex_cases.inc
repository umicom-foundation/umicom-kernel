/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_shell/hex_cases.inc
 *
 * PURPOSE:
 *   Verify byte inspection, short reads, terminal safety and shared descriptor ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
static UmicomU8 hexData[5000];
static UmicomKernelVfsStatus HexShortRead(void *context, UmicomKernelVfsNodeId node,
    UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *read)
{
    ++rangeCalls;
    return rangeOriginal->read(context, node, offset, destination, bytes < 7U ? bytes : 7U, read);
}
static void HexCheckRows(UmicomSize offset, UmicomSize count)
{
    static const char digits[] = "0123456789abcdef";
    const char *line = transcript;
    UmicomSize inspected = 0U;
    while (inspected < count) {
        const UmicomSize width = count - inspected < 16U ? count - inspected : 16U;
        char address[16];
        CHECK(snprintf(address, sizeof(address), "%08x  ", (unsigned)(offset + inspected)) == 10);
        CHECK(!memcmp(line, address, 10U));
        for (UmicomSize i = 0U; i < 16U; ++i) {
            const UmicomU8 byte = i < width ? hexData[offset + inspected + i] : 0U;
            CHECK(line[10U + i * 3U] == (i < width ? digits[byte >> 4U] : ' '));
            CHECK(line[11U + i * 3U] == (i < width ? digits[byte & 15U] : ' '));
            CHECK(line[12U + i * 3U] == ' ');
            if (i < width)
                CHECK(line[59U + i] == (byte >= 32U && byte <= 126U ? (char)byte : '.'));
        }
        CHECK(line[58] == '|' && line[59U + width] == '|');
        CHECK(line[60U + width] == '\r' && line[61U + width] == '\n');
        line += 62U + width;
        inspected += width;
    }
    CHECK(!strncmp(line, "file.hex offset=", 16U));
    for (const unsigned char *p = (const unsigned char *)transcript; *p != 0U; ++p)
        CHECK((*p >= 32U && *p <= 126U) || *p == '\r' || *p == '\n');
}
static void HexCase(const char *name)
{
    CHECK(Command("create '/notes/raw bytes'") == UMICOM_SHELL_OK);
    for (UmicomSize i = 0U; i < sizeof(hexData); ++i) hexData[i] = (UmicomU8)(i % 256U);
    UmicomKernelFileDescriptor fd = 0U;
    UmicomSize count = 0U;
    CHECK(UmicomKernelVfsOpen(&shell.client, "/notes/raw bytes", UMICOM_VFS_RIGHT_WRITE,
        UMICOM_FALSE, &fd) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsWrite(&shell.client, fd, hexData, sizeof(hexData), &count) == UMICOM_VFS_OK);
    CHECK(count == sizeof(hexData));
    CHECK(UmicomKernelVfsClose(&shell.client, fd) == UMICOM_VFS_OK);
    ResetOutput();
    const UmicomU64 allocated = UmicomModelMemory().allocatedFrames;
    if (!strcmp(name, "hex-all-bytes")) {
        CHECK(Command("hex '/notes/raw bytes' 0 256") == UMICOM_SHELL_OK);
        HexCheckRows(0U, 256U);
    } else if (!strcmp(name, "hex-page-boundary")) {
        CHECK(Command("hex '/notes/raw bytes' 4090 20") == UMICOM_SHELL_OK);
        HexCheckRows(4090U, 20U);
    } else if (!strcmp(name, "hex-budget")) {
        CHECK(Command("hex '/notes/raw bytes' 0 4096") == UMICOM_SHELL_OK);
        HexCheckRows(0U, 4096U);
        CHECK(strstr(transcript, "read=4096 next=4096 eof=no") != NULL);
    } else if (!strcmp(name, "hex-tail")) {
        CHECK(Command("hex '/notes/raw bytes' 4993 20") == UMICOM_SHELL_OK);
        HexCheckRows(4993U, 7U);
        CHECK(strstr(transcript, "read=7 next=5000 eof=yes") != NULL);
    } else if (!strcmp(name, "hex-eof")) {
        CHECK(Command("hex '/notes/raw bytes' 5000 20") == UMICOM_SHELL_OK);
        CHECK(!strcmp(transcript, "file.hex offset=5000 requested=20 read=0 next=5000 eof=yes\r\n"));
    } else if (!strcmp(name, "hex-zero")) {
        CHECK(Command("hex '/notes/raw bytes' 5 0") == UMICOM_SHELL_OK);
        CHECK(!strcmp(transcript, "file.hex offset=5 requested=0 read=0 next=5 eof=no\r\n"));
    } else if (!strcmp(name, "hex-short-read") || !strcmp(name, "hex-partial-error")) {
        rangeOriginal = shell.vfs.operations;
        rangeOperations = *rangeOriginal; rangeCalls = 0U;
        rangePartial = UMICOM_TRUE;
        rangeOperations.read = !strcmp(name, "hex-short-read") ? HexShortRead : RangeRefusedRead;
        shell.vfs.operations = &rangeOperations;
        const UmicomKernelShellStatus status = Command("hex '/notes/raw bytes' 0 35");
        shell.vfs.operations = rangeOriginal;
        if (!strcmp(name, "hex-short-read")) {
            CHECK(status == UMICOM_SHELL_OK && rangeCalls == 5U);
            HexCheckRows(0U, 35U);
        } else {
            CHECK(status == UMICOM_SHELL_IO_ERROR && rangeCalls == 1U);
            HexCheckRows(0U, 3U);
            CHECK(strstr(transcript, "requested=35 read=3 next=3 eof=no") != NULL);
            CHECK(strstr(transcript, "file error:") != NULL);
        }
    } else if (!strcmp(name, "hex-independent-position")) {
        CHECK(Open("/notes/raw bytes", &fd) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsSeek(&shell.client, fd, 99U) == UMICOM_VFS_OK);
        CHECK(Command("hex '/notes/raw bytes' 20 19") == UMICOM_SHELL_OK);
        HexCheckRows(20U, 19U);
        UmicomU8 byte = 0U;
        CHECK(UmicomKernelVfsRead(&shell.client, fd, &byte, 1U, &count) == UMICOM_VFS_OK);
        CHECK(count == 1U && byte == hexData[99]);
        CHECK(UmicomKernelVfsClose(&shell.client, fd) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "hex-invalid")) {
        const char *const bad[] = {"hex '/notes/raw bytes' 0 4097", "hex '/notes/raw bytes' -1 1",
            "hex '/notes/raw bytes' 131073 0", "hex '/notes/raw bytes' 0 1x",
            "hex '/notes/raw bytes' 0 18446744073709551616"};
        for (UmicomSize i = 0U; i < sizeof(bad)/sizeof(bad[0]); ++i) {
            ResetOutput(); CHECK(Command(bad[i]) == UMICOM_SHELL_RANGE);
            CHECK(strstr(transcript, "hex requires a decimal") != NULL);
        }
        CHECK(Command("hex /notes 0 1") == UMICOM_SHELL_IO_ERROR);
        CHECK(Command("hex /notes/absent 0 1") == UMICOM_SHELL_IO_ERROR);
        CHECK(Command("hex '/notes/raw bytes' 0") == UMICOM_SHELL_UNKNOWN_COMMAND);
    } else if (!strcmp(name, "hex-reentry")) {
        tryReentry = UMICOM_TRUE;
        CHECK(Command("hex '/notes/raw bytes' 0 20") == UMICOM_SHELL_OK);
        tryReentry = UMICOM_FALSE; CHECK(refusedReentries != 0U);
        HexCheckRows(0U, 20U);
    } else CHECK(0);
    CHECK(shell.descriptor == 0U && !shell.busy);
    CHECK(UmicomModelMemory().allocatedFrames == allocated);
    CHECK(Open("/notes/raw bytes", &fd) == UMICOM_VFS_OK);
    UmicomU8 bytes[5000];
    CHECK(UmicomKernelVfsRead(&shell.client, fd, bytes, sizeof(bytes), &count) == UMICOM_VFS_OK);
    CHECK(count == sizeof(bytes) && !memcmp(bytes, hexData, sizeof(bytes)));
    CHECK(UmicomKernelVfsClose(&shell.client, fd) == UMICOM_VFS_OK);
}
