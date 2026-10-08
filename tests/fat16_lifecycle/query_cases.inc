/* Umicom Kernel committed inspection tests over the real lifecycle transport.
 * Queries must preserve mutation evidence and publish no partial caller values.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_lifecycle_query.h"
static UmicomKernelFat16Query queryRequest;
static UmicomKernelFat16QueryResult queryResult;
static UmicomKernelFat16LifecycleResult queryHistory;
static UmicomKernelFat16CommitResult queryCommitHistory;
static UmicomKernelFat16LifecycleCommitter queryCopiedOwner;
static char queryPath[UMICOM_FAT16_PATH_BYTES];

/* Verify the complete value, including padding and unused arrays. */
static void QueryOutputUnchanged(void)
{
    const unsigned char *const bytes = (const unsigned char *)&queryResult;
    for (UmicomSize i = 0U; i < sizeof(queryResult); ++i) CHECK(bytes[i] == 0xa5U);
}

static void QueryPrepare(const char *fixture)
{
    DirectoryPrepare(fixture, "mkdir.root"); LCOpen();
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK && LCFinish() == UMICOM_FAT16_UPDATE_OK);
    DirectoryRequest(UMICOM_FAT16_LIFECYCLE_CREATE, "/WORK/NOTE.TXT");
    lcRequest.input = "hello world"; lcRequest.bytes = 11U;
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK && LCFinish() == UMICOM_FAT16_UPDATE_OK);
    memcpy(lcExpected, commitVisible, sizeof(lcExpected));
    memcpy(&queryHistory, &lcOwner.lastResult, sizeof(queryHistory));
    memcpy(&queryCommitHistory, &lcOwner.commit.lastResult, sizeof(queryCommitHistory));
    memset(&queryRequest, 0, sizeof(queryRequest));
    strcpy(queryPath, "/WORK/NOTE.TXT");
    queryRequest.path = queryPath;
    queryRequest.kind = UMICOM_FAT16_QUERY_STAT;
    memset(&queryResult, 0xa5, sizeof(queryResult));
}
static void QueryHistoryPreserved(void)
{
    CHECK(lcOwner.committedOperations == 2U);
    CHECK(!memcmp(&queryHistory, &lcOwner.lastResult, sizeof(queryHistory)));
    CHECK(!memcmp(&queryCommitHistory, &lcOwner.commit.lastResult, sizeof(queryCommitHistory)));
    CommitEqual(commitVisible, lcExpected, sizeof(lcExpected));
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected));
    CHECK(!lcOwner.busy && !lcOwner.commit.busy && !lcOwner.commit.updater.busy);
}
static void QuerySelect(const char *name)
{
    if (!strcmp(name, "list")) { queryRequest.kind = UMICOM_FAT16_QUERY_LIST; strcpy(queryPath, "/WORK"); }
    else if (!strcmp(name, "read") || !strcmp(name, "eof")) {
        queryRequest.kind = UMICOM_FAT16_QUERY_READ; queryRequest.capacity = 8U;
        queryRequest.offset = !strcmp(name, "eof") ? ~(UmicomU64)0U : 6U;
    } else CHECK(!strcmp(name, "stat"));
}
static void QuerySuccess(const char *fixture, const char *name)
{
    QueryPrepare(fixture); QuerySelect(name);
    const UmicomU32 writes = commitWrites, flushes = commitFlushes;
    CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) == UMICOM_FAT16_UPDATE_OK);
    CHECK(queryResult.kind == queryRequest.kind && queryResult.committedOperations == 2U);
    CHECK(queryResult.metadata.writeTimestamp.state == UMICOM_FAT16_TIMESTAMP_VALID);
    CHECK(queryResult.metadata.writeTimestamp.value.second == 56U);
    if (!strcmp(name, "list")) {
        CHECK(queryResult.directory.count == 1U);
        CHECK(!strcmp(queryResult.directory.entries[0].name, "NOTE.TXT"));
        CHECK(queryResult.directory.entries[0].bytes == 11U);
    } else {
        CHECK(!strcmp(queryResult.metadata.entry.name, "NOTE.TXT"));
        CHECK(queryResult.metadata.entry.bytes == 11U);
        if (!strcmp(name, "read")) CHECK(queryResult.bytes == 5U && !memcmp(queryResult.data, "world", 5U));
        else CHECK(queryResult.bytes == 0U);
    }
    for (UmicomSize i = queryResult.bytes; i < sizeof(queryResult.data); ++i) CHECK(!queryResult.data[i]);
    CHECK(commitWrites == writes && commitFlushes == flushes);
    QueryHistoryPreserved();
    /* Browsing does not consume the next mutation opportunity. */
    DirectoryRequest(UMICOM_FAT16_LIFECYCLE_DELETE, "/WORK/NOTE.TXT");
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK && LCFinish() == UMICOM_FAT16_UPDATE_OK);
    CHECK(lcOwner.committedOperations == 3U); LCClose();
}
static void QueryGuard(const char *fixture, const char *name)
{
    QueryPrepare(fixture);
    const UmicomKernelFat16Query *request = &queryRequest;
    UmicomKernelFat16QueryResult *output = &queryResult;
    UmicomKernelFat16LifecycleCommitter *owner = &lcOwner;
    UmicomKernelFat16UpdateStatus expected = UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!strcmp(name, "null-request")) request = NULL;
    else if (!strcmp(name, "null-output")) output = NULL;
    else if (!strcmp(name, "null-path")) queryRequest.path = NULL;
    else if (!strcmp(name, "request-alignment")) request = (const UmicomKernelFat16Query *)((const unsigned char *)&queryRequest + 1U);
    else if (!strcmp(name, "output-alignment")) output = (UmicomKernelFat16QueryResult *)((unsigned char *)&queryResult + 1U);
    else if (!strcmp(name, "output-domain")) output = (UmicomKernelFat16QueryResult *)&domain;
    else if (!strcmp(name, "request-owner")) request = (const UmicomKernelFat16Query *)&lcOwner;
    else if (!strcmp(name, "output-owner")) output = (UmicomKernelFat16QueryResult *)&lcOwner;
    else if (!strcmp(name, "request-output")) request = (const UmicomKernelFat16Query *)&queryResult;
    else if (!strcmp(name, "path-owner")) queryRequest.path = (const char *)&lcOwner;
    else if (!strcmp(name, "path-output")) queryRequest.path = (const char *)&queryResult;
    else if (!strcmp(name, "path-request")) queryRequest.path = (const char *)&queryRequest;
    else if (!strcmp(name, "unterminated")) memset(queryPath, 'X', sizeof(queryPath));
    else if (!strcmp(name, "kind")) queryRequest.kind = UMICOM_FAT16_QUERY_NONE;
    else if (!strcmp(name, "stat-offset")) queryRequest.offset = 1U;
    else if (!strcmp(name, "stat-capacity")) queryRequest.capacity = 1U;
    else if (!strcmp(name, "read-zero")) queryRequest.kind = UMICOM_FAT16_QUERY_READ;
    else if (!strcmp(name, "read-overflow")) { queryRequest.kind = UMICOM_FAT16_QUERY_READ; queryRequest.capacity = UMICOM_FAT16_READ_BYTES + 1U; }
    else if (!strcmp(name, "copied")) { queryCopiedOwner = lcOwner; owner = &queryCopiedOwner; expected = UMICOM_FAT16_UPDATE_BAD_STATE; }
    else if (!strcmp(name, "busy")) { lcOwner.busy = UMICOM_TRUE; expected = UMICOM_FAT16_UPDATE_BUSY; }
    else if (!strcmp(name, "failed")) { lcOwner.commit.state = UMICOM_FAT16_COMMIT_FAILED; expected = UMICOM_FAT16_UPDATE_BAD_STATE; }
    else CHECK(0);
    const UmicomU32 events = commitEventCount;
    CHECK(UmicomKernelFat16LifecycleQuery(owner, request, output) == expected);
    CHECK(events == commitEventCount);
    QueryOutputUnchanged();
    lcOwner.busy = UMICOM_FALSE;
    QueryHistoryPreserved(); LCClose();
}
static void QueryRefusal(const char *fixture, const char *name)
{
    QueryPrepare(fixture);
    UmicomKernelDiskStatus expected = UMICOM_DISK_NOT_FOUND;
    if (!strcmp(name, "missing")) strcpy(queryPath, "/MISSING");
    else if (!strcmp(name, "file-list")) { queryRequest.kind = UMICOM_FAT16_QUERY_LIST; expected = UMICOM_DISK_NOT_DIRECTORY; }
    else if (!strcmp(name, "directory-read")) {
        queryRequest.kind = UMICOM_FAT16_QUERY_READ; queryRequest.capacity = 10U;
        strcpy(queryPath, "/WORK"); expected = UMICOM_DISK_IS_DIRECTORY;
    } else CHECK(0);
    const UmicomU32 writes = commitWrites, flushes = commitFlushes;
    CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcOwner.commit.updater.lastDiskStatus == expected);
    QueryOutputUnchanged();
    CHECK(writes == commitWrites && flushes == commitFlushes);
    QueryHistoryPreserved(); LCClose();
}
static void QueryFailures(const char *fixture, const char *name)
{
    QueryPrepare(fixture); QuerySelect(name);
    const UmicomU32 before = commitReads;
    CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 count = commitReads - before; LCClose();
    /* Fail every read position, including BPB/FAT admission and the final data
     * sector. All caller bytes remain sentinels, even after a partial read. */
    for (UmicomU32 i = 1U; i <= count; ++i) {
        QueryPrepare(fixture); QuerySelect(name);
        commitFailRead = commitReads + i;
        const UmicomU32 writes = commitWrites, flushes = commitFlushes;
        CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) != UMICOM_FAT16_UPDATE_OK);
        QueryOutputUnchanged();
        CHECK(writes == commitWrites && flushes == commitFlushes);
        QueryHistoryPreserved(); LCClose();
    }
    printf("query=%s read-failure-positions=%u\n", name, count);
}
static void QueryStaged(const char *fixture)
{
    QueryPrepare(fixture);
    DirectoryRequest(UMICOM_FAT16_LIFECYCLE_DELETE, "/WORK/NOTE.TXT");
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 events = commitEventCount;
    CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) == UMICOM_FAT16_UPDATE_BAD_STATE);
    CHECK(events == commitEventCount);
    QueryOutputUnchanged();
    CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK); LCClose();
}
static void QueryConsole(const char *fixture)
{
    DirectoryPrepare(fixture, "mkdir.root");
    memset(&commitShell, 0, sizeof(commitShell)); commitShell.output = LCConsoleOutput;
    CHECK(FileConsoleCommand(&commitShell, "fatfsopen 0 0") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfstime 2044-02-29T23:58:57") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatmkdir /WORK") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfsls /") == UMICOM_SHELL_IO_ERROR);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CommitClearTranscript();
    CHECK(FileConsoleCommand(&commitShell, "fatfsls /") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "directory WORK") && strstr(transcript, "accepted-operations=1"));
    CHECK(FileConsoleCommand(&commitShell, "fatcreate /WORK/NOTE.TXT 'hello world'") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CommitClearTranscript();
    /* Output callbacks exercise cross-command reentry while the query result
     * remains retained by the console. */
    lcConsoleOutput = UMICOM_TRUE;
    CHECK(FileConsoleCommand(&commitShell, "fatfsread /WORK/NOTE.TXT 6 20") == UMICOM_SHELL_OK);
    lcConsoleOutput = UMICOM_FALSE;
    CHECK(lcConsoleReentries > 0U);
    CHECK(strstr(transcript, "bytes=5") && strstr(transcript, "world"));
    CHECK(FileConsoleCommand(&commitShell, "fatfsstat /WORK/NOTE.TXT") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "write-time=2044-02-29T23:58:56"));
    CHECK(FileConsoleCommand(&commitShell, "fatfsread /WORK/NOTE.TXT 0 4097") == UMICOM_SHELL_INVALID_ARGUMENT);
    CHECK(FileConsoleCommand(&commitShell, "fatfsclose") == UMICOM_SHELL_OK);
    CHECK(!Allocated());
}
/* Platform callbacks must not enter a second query or mutate the same owner.
 * The wrapper calls the real clock after checking the outer busy boundary. */
static UmicomU64 (*queryClockOriginal)(void *);
static UmicomU32 queryClockCalls;
static const char *queryClockMode;
static UmicomU64 QueryClock(void *context)
{
    ++queryClockCalls;
    CHECK(UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16LifecycleClose(&lcOwner) == UMICOM_FAT16_UPDATE_BUSY);
    QueryOutputUnchanged();
    const UmicomU64 now = queryClockOriginal(context);
    if (!strcmp(queryClockMode, "timeout") && queryClockCalls > 1U)
        return now + UMICOM_FAT16_UPDATE_OPERATION_TICKS;
    if (!strcmp(queryClockMode, "rollback") && queryClockCalls > 1U) return 0U;
    return now;
}
static void QueryCallbacks(const char *fixture, const char *name)
{
    QueryPrepare(fixture); QuerySelect("read");
    queryClockMode = name; queryClockCalls = 0U;
    queryClockOriginal = domain.operations.clock;
    domain.operations.clock = QueryClock;
    const UmicomU32 writes = commitWrites, flushes = commitFlushes;
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16LifecycleQuery(&lcOwner, &queryRequest, &queryResult);
    domain.operations.clock = queryClockOriginal;
    CHECK(queryClockCalls > 1U && writes == commitWrites && flushes == commitFlushes);
    if (!strcmp(name, "reentry")) CHECK(status == UMICOM_FAT16_UPDATE_OK);
    else { CHECK(status != UMICOM_FAT16_UPDATE_OK); QueryOutputUnchanged(); }
    QueryHistoryPreserved(); LCClose();
}
static void QueryCase(const char *fixture, const char *name)
{
    if (!strncmp(name, "success.", 8U)) QuerySuccess(fixture, name + 8U);
    else if (!strncmp(name, "guard.", 6U)) QueryGuard(fixture, name + 6U);
    else if (!strncmp(name, "refusal.", 8U)) QueryRefusal(fixture, name + 8U);
    else if (!strncmp(name, "failures.", 9U)) QueryFailures(fixture, name + 9U);
    else if (!strncmp(name, "callbacks.", 10U)) QueryCallbacks(fixture, name + 10U);
    else if (!strcmp(name, "staged")) QueryStaged(fixture);
    else { CHECK(!strcmp(name, "console")); QueryConsole(fixture); }
}
