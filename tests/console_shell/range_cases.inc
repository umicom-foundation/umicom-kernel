/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_shell/range_cases.inc
 *
 * PURPOSE:
 *   Verify bounded console reads preserve file data, descriptor positions and honest partial results.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

static UmicomU8 rangeData[5000];
static UmicomKernelVfsOperations rangeOperations;
static const UmicomKernelVfsOperations *rangeOriginal;
static unsigned rangeCalls;
static UmicomBoolean rangePartial;
static UmicomKernelVfsStatus RangeRefusedRead(void *context, UmicomKernelVfsNodeId node,
    UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *read)
{
    ++rangeCalls;
    *read = 0U;
    if (rangePartial) {
        CHECK(bytes >= 3U);
        CHECK(rangeOriginal->read(context, node, offset, destination, 3U, read) == UMICOM_VFS_OK);
    }
    return UMICOM_VFS_IO_ERROR;
}
static void RangeCase(const char *name)
{
    CHECK(Command("create '/notes/a b'") == UMICOM_SHELL_OK);
    for (UmicomSize i = 0U; i < sizeof(rangeData); ++i) rangeData[i] = (UmicomU8)('A' + i % 26U);
    UmicomKernelFileDescriptor writer = 0U;
    UmicomSize count = 0U;
    CHECK(UmicomKernelVfsOpen(&shell.client, "/notes/a b", UMICOM_VFS_RIGHT_WRITE,
        UMICOM_FALSE, &writer) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsWrite(&shell.client, writer, rangeData, sizeof(rangeData), &count) == UMICOM_VFS_OK);
    CHECK(count == sizeof(rangeData));
    CHECK(UmicomKernelVfsClose(&shell.client, writer) == UMICOM_VFS_OK);
    ResetOutput();
    const UmicomU64 allocated = UmicomModelMemory().allocatedFrames;
    if (!strcmp(name, "range-middle")) {
        CHECK(Command("read '/notes/a b' 5 7") == UMICOM_SHELL_OK);
        CHECK(!strcmp(transcript, "FGHIJKL\r\nfile.read offset=5 requested=7 read=7 next=12 eof=no\r\n"));
    } else if (!strcmp(name, "range-page-boundary")) {
        CHECK(Command("read '/notes/a b' 4090 20") == UMICOM_SHELL_OK);
        CHECK(!memcmp(transcript, rangeData + 4090U, 20U));
        CHECK(strstr(transcript, "read=20 next=4110 eof=no") != NULL);
    } else if (!strcmp(name, "range-budget")) {
        CHECK(Command("read '/notes/a b' 0 4096") == UMICOM_SHELL_OK);
        CHECK(!memcmp(transcript, rangeData, 4096U));
        CHECK(strstr(transcript, "requested=4096 read=4096 next=4096 eof=no") != NULL);
    } else if (!strcmp(name, "range-tail")) {
        CHECK(Command("read '/notes/a b' 4995 100") == UMICOM_SHELL_OK);
        CHECK(!memcmp(transcript, rangeData + 4995U, 5U));
        CHECK(strstr(transcript, "read=5 next=5000 eof=yes") != NULL);
    } else if (!strcmp(name, "range-eof")) {
        CHECK(Command("read '/notes/a b' 5000 5") == UMICOM_SHELL_OK);
        CHECK(!strcmp(transcript, "\r\nfile.read offset=5000 requested=5 read=0 next=5000 eof=yes\r\n"));
    } else if (!strcmp(name, "range-beyond-eof")) {
        CHECK(Command("read '/notes/a b' 131072 4096") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "read=0 next=131072 eof=yes") != NULL);
    } else if (!strcmp(name, "range-zero")) {
        CHECK(Command("read '/notes/a b' 4 0") == UMICOM_SHELL_OK);
        CHECK(!strcmp(transcript, "\r\nfile.read offset=4 requested=0 read=0 next=4 eof=no\r\n"));
    } else if (!strcmp(name, "range-empty")) {
        CHECK(Command("create /notes/empty") == UMICOM_SHELL_OK); ResetOutput();
        CHECK(Command("read /notes/empty 0 10") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "read=0 next=0 eof=yes") != NULL);
    } else if (!strcmp(name, "range-binary")) {
        static const UmicomU8 bytes[] = {0U,27U,255U,10U,13U};
        CHECK(UmicomKernelVfsOpen(&shell.client, "/notes/a b", UMICOM_VFS_RIGHT_WRITE,
            UMICOM_FALSE, &writer) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&shell.client, writer, bytes, sizeof(bytes), &count) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsClose(&shell.client, writer) == UMICOM_VFS_OK);
        CHECK(Command("read '/notes/a b' 0 5") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "\\x00\\x1b\\xff\r\n\\x0d\r\nfile.read") == transcript);
        CHECK(strchr(transcript, 27) == NULL);
        /* Restore the deliberately changed bytes before the common readback. */
        CHECK(UmicomKernelVfsOpen(&shell.client, "/notes/a b", UMICOM_VFS_RIGHT_WRITE,
            UMICOM_FALSE, &writer) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&shell.client, writer, rangeData, sizeof(bytes), &count) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsClose(&shell.client, writer) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "range-partial-error") || !strcmp(name, "range-read-error")) {
        rangeOriginal = shell.vfs.operations; rangeOperations = *rangeOriginal;
        rangeOperations.read = RangeRefusedRead; shell.vfs.operations = &rangeOperations;
        rangePartial = !strcmp(name, "range-partial-error") ? UMICOM_TRUE : UMICOM_FALSE;
        CHECK(Command("read '/notes/a b' 0 10") == UMICOM_SHELL_IO_ERROR);
        CHECK(rangeCalls == 1U); CHECK(strstr(transcript, "file error: io-error") != NULL);
        CHECK(strstr(transcript, rangePartial ? "read=3 next=3 eof=no" : "read=0 next=0 eof=no") != NULL);
        shell.vfs.operations = rangeOriginal;
    } else if (!strcmp(name, "range-independent-position")) {
        UmicomKernelFileDescriptor reader = 0U; char bytes[3];
        CHECK(Open("/notes/a b", &reader) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsSeek(&shell.client, reader, 100U) == UMICOM_VFS_OK);
        CHECK(Command("read '/notes/a b' 5 7") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelVfsRead(&shell.client, reader, bytes, sizeof(bytes), &count) == UMICOM_VFS_OK);
        CHECK(count == sizeof(bytes) && !memcmp(bytes, rangeData + 100U, sizeof(bytes)));
        CHECK(UmicomKernelVfsClose(&shell.client, reader) == UMICOM_VFS_OK);
    } else if (!strcmp(name, "range-reentry")) {
        tryReentry = UMICOM_TRUE;
        CHECK(Command("read '/notes/a b' 0 20") == UMICOM_SHELL_OK);
        tryReentry = UMICOM_FALSE; CHECK(refusedReentries != 0U);
    } else if (!strcmp(name, "range-repeated")) {
        for (unsigned i = 0U; i < 100U; ++i) {
            ResetOutput(); CHECK(Command("read '/notes/a b' 5 7") == UMICOM_SHELL_OK);
            CHECK(shell.descriptor == 0U);
        }
    } else if (!strcmp(name, "range-errors")) {
        const char *const requests[] = {"read /notes/absent 0 1", "read /notes 0 0"};
        for (UmicomSize i = 0U; i < sizeof(requests)/sizeof(requests[0]); ++i) {
            ResetOutput(); CHECK(Command(requests[i]) == UMICOM_SHELL_IO_ERROR);
            CHECK(strstr(transcript, "file error:") != NULL); CHECK(shell.descriptor == 0U);
        }
    } else if (!strcmp(name, "range-invalid")) {
        const char *const requests[] = {
            "read '/notes/a b' 0 4097", "read '/notes/a b' 131073 0",
            "read '/notes/a b' -1 1", "read '/notes/a b' 1 -1",
            "read '/notes/a b' 18446744073709551616 1", "read '/notes/a b' 0 18446744073709551616",
            "read '/notes/a b' 1x 2", "read '/notes/a b' 0 ''"};
        for (UmicomSize i = 0U; i < sizeof(requests)/sizeof(requests[0]); ++i) {
            ResetOutput(); CHECK(Command(requests[i]) == UMICOM_SHELL_RANGE);
            CHECK(strstr(transcript, "read requires a decimal") != NULL); CHECK(shell.descriptor == 0U);
        }
        CHECK(Command("read '/notes/a b' 0") == UMICOM_SHELL_UNKNOWN_COMMAND);
        CHECK(Command("read '/notes/a b' 0 1 extra") == UMICOM_SHELL_SYNTAX);
    } else CHECK(0);
    CHECK(shell.descriptor == 0U && !shell.busy);
    CHECK(UmicomModelMemory().allocatedFrames == allocated);
    UmicomKernelFileDescriptor readback = 0U;
    CHECK(Open("/notes/a b", &readback) == UMICOM_VFS_OK);
    UmicomU8 bytes[5000];
    CHECK(UmicomKernelVfsRead(&shell.client, readback, bytes, sizeof(bytes), &count) == UMICOM_VFS_OK);
    CHECK(count == sizeof(bytes) && !memcmp(bytes, rangeData, sizeof(bytes)));
    CHECK(UmicomKernelVfsClose(&shell.client, readback) == UMICOM_VFS_OK);
}
