/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_update_console.c
 *
 * Keep one explicit writable FAT16 utility lease for a trusted console. Each
 * update remains inside the existing file allocation, and every successful
 * write still needs an explicit fatflush. Closing or powering off releases
 * ownership without submitting an implicit flush or retrying file data.
 *
 * Result records distinguish a confirmed caller-byte prefix from an exposed,
 * unconfirmed physical sector. That entire sector, including adjacent file
 * bytes or slack, is at risk after an uncertain write. Refused commands must
 * not relabel an earlier write record as evidence for the refused attempt.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_update_console.h"

static UmicomKernelFat16Updater umicomFatConsoleUpdater;
static UmicomKernelConsoleShell *umicomFatUpdateConsole;
static UmicomBoolean umicomFatUpdateConsoleBusy;

static void UmicomFatConsoleClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static UmicomBoolean UmicomFatConsoleEqual(const char *left, const char *right)
{
    for (UmicomSize i = 0U;; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
}
static void UmicomFatConsoleText(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomSize count = 0U;
    while (text[count]) ++count;
    shell->output(shell->outputContext, text, count);
}
static void UmicomFatConsoleNumber(UmicomKernelConsoleShell *shell, UmicomU64 number)
{
    char digits[20]; UmicomSize count = 0U;
    do { digits[count++] = (char)('0' + number % 10U); number /= 10U; } while (number);
    while (count) shell->output(shell->outputContext, &digits[--count], 1U);
}
static const char *UmicomFatConsoleOutcome(UmicomKernelFat16UpdateOutcome outcome)
{
    switch (outcome) {
        case UMICOM_FAT16_UPDATE_NOT_SUBMITTED: return "not-submitted";
        case UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED: return "partial-confirmed";
        case UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED: return "submitted-unconfirmed";
        case UMICOM_FAT16_UPDATE_COMPLETED: return "completed";
    }
    return "unknown";
}
static const char *UmicomFatConsoleBlockOutcome(UmicomKernelBlockMutationOutcome outcome)
{
    switch (outcome) {
        case UMICOM_BLOCK_NOT_SUBMITTED: return "not-submitted";
        case UMICOM_BLOCK_SUBMITTED_UNCONFIRMED: return "submitted-unconfirmed";
        case UMICOM_BLOCK_COMPLETED: return "completed";
    }
    return "unchanged";
}
static void UmicomFatConsoleState(UmicomKernelConsoleShell *shell)
{
    UmicomFatConsoleText(shell, "fat.update-state=");
    UmicomFatConsoleText(shell, umicomFatConsoleUpdater.state == UMICOM_FAT16_UPDATER_OPEN ? "open" :
        (umicomFatConsoleUpdater.state == UMICOM_FAT16_UPDATER_CLOSING ? "closing" :
        (umicomFatConsoleUpdater.state == UMICOM_FAT16_UPDATER_CLOSED ? "closed" : "unused")));
    UmicomFatConsoleText(shell, " slot="); UmicomFatConsoleNumber(shell, umicomFatConsoleUpdater.slot);
    UmicomFatConsoleText(shell, " partition="); UmicomFatConsoleNumber(shell, umicomFatConsoleUpdater.partition);
    UmicomFatConsoleText(shell, " needs-flush=");
    UmicomFatConsoleNumber(shell, umicomFatConsoleUpdater.needsFlush ? 1U : 0U);
    UmicomFatConsoleText(shell, " write-uncertain=");
    UmicomFatConsoleNumber(shell, umicomFatConsoleUpdater.writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static void UmicomFatConsoleReport(UmicomKernelConsoleShell *shell, const char *operation,
    UmicomKernelFat16UpdateStatus status)
{
    UmicomFatConsoleText(shell, operation); UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(status));
    UmicomFatConsoleText(shell, " last-disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(umicomFatConsoleUpdater.lastDiskStatus));
    UmicomFatConsoleText(shell, " last-block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(umicomFatConsoleUpdater.lastBlockStatus));
    UmicomFatConsoleText(shell, " last-cleanup=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(umicomFatConsoleUpdater.lastCleanupStatus));
    UmicomFatConsoleText(shell, "\r\n");
    UmicomFatConsoleState(shell);
}
static void UmicomFatConsoleResult(UmicomKernelConsoleShell *shell, const char *label,
    const UmicomKernelFat16UpdateResult *result)
{
    UmicomFatConsoleText(shell, label); UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomFatConsoleOutcome(result->outcome));
    UmicomFatConsoleText(shell, " status=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(result->status));
    UmicomFatConsoleText(shell, " disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(result->diskStatus));
    UmicomFatConsoleText(shell, " block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(result->blockStatus));
    UmicomFatConsoleText(shell, " last-block-outcome=");
    UmicomFatConsoleText(shell, UmicomFatConsoleBlockOutcome(result->lastBlockOutcome));
    UmicomFatConsoleText(shell, "\r\n");
    UmicomFatConsoleText(shell, "fat.result.offset="); UmicomFatConsoleNumber(shell, result->offset);
    UmicomFatConsoleText(shell, " requested="); UmicomFatConsoleNumber(shell, result->requestedBytes);
    UmicomFatConsoleText(shell, " confirmed="); UmicomFatConsoleNumber(shell, result->confirmedBytes);
    UmicomFatConsoleText(shell, " submitted="); UmicomFatConsoleNumber(shell, result->submittedBytes);
    UmicomFatConsoleText(shell, " completed-sectors="); UmicomFatConsoleNumber(shell, result->completedSectors);
    UmicomFatConsoleText(shell, " submitted-sectors="); UmicomFatConsoleNumber(shell, result->submittedSectors);
    UmicomFatConsoleText(shell, " needs-flush-at-return=");
    UmicomFatConsoleNumber(shell, result->needsFlush ? 1U : 0U);
    UmicomFatConsoleText(shell, " write-uncertain-at-return=");
    UmicomFatConsoleNumber(shell, result->writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
    if (result->uncertainBytes) {
        UmicomFatConsoleText(shell, "fat.uncertain.file-offset=");
        UmicomFatConsoleNumber(shell, result->uncertainOffset);
        UmicomFatConsoleText(shell, " caller-bytes="); UmicomFatConsoleNumber(shell, result->uncertainBytes);
        UmicomFatConsoleText(shell, " whole-sector-lba="); UmicomFatConsoleNumber(shell, result->uncertainSector);
        UmicomFatConsoleText(shell, " sector-bytes-at-risk=512\r\n");
        UmicomFatConsoleText(shell, "The entire sector may have changed. Confirmed bytes do not identify a safe retry point.\r\n");
    }
}
static UmicomKernelFat16UpdateStatus UmicomFatConsoleClose(UmicomKernelConsoleShell *shell)
{
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16UpdateClose(&umicomFatConsoleUpdater);
    if (status == UMICOM_FAT16_UPDATE_OK &&
        (umicomFatConsoleUpdater.needsFlush || umicomFatConsoleUpdater.writeUncertain))
        UmicomFatConsoleText(shell, "fat.close=resources-released; pending or uncertain write evidence remains; no flush was submitted\r\n");
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!shell || !shell->output) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!umicomFatUpdateConsole) return UMICOM_FAT16_UPDATE_OK;
    if (umicomFatUpdateConsole != shell) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (umicomFatUpdateConsoleBusy) return UMICOM_FAT16_UPDATE_BUSY;
    umicomFatUpdateConsoleBusy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomFatConsoleClose(shell);
    umicomFatUpdateConsoleBusy = UMICOM_FALSE;
    return status;
}
UmicomKernelShellStatus UmicomKernelFat16UpdateCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *const name = command->bytes + command->offsets[0];
    const UmicomBoolean open = UmicomFatConsoleEqual(name, "fatwriteopen");
    const UmicomBoolean write = UmicomFatConsoleEqual(name, "fatwrite");
    const UmicomBoolean flush = UmicomFatConsoleEqual(name, "fatflush");
    const UmicomBoolean info = UmicomFatConsoleEqual(name, "fatwriteinfo");
    const UmicomBoolean close = UmicomFatConsoleEqual(name, "fatwriteclose");
    if (!open && !write && !flush && !info && !close) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    const UmicomSize expected = open ? 3U : (write ? 4U : 1U);
    if (command->count != expected) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomFatUpdateConsoleBusy) return UMICOM_SHELL_BUSY;
    if (umicomFatUpdateConsole && umicomFatUpdateConsole != shell) return UMICOM_SHELL_BAD_STATE;
    umicomFatUpdateConsoleBusy = UMICOM_TRUE;
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    if (open) {
        UmicomU64 slot = 0U, partition = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
            !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
            slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS) {
            umicomFatUpdateConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
        const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
        if (block != UMICOM_BLOCK_OK) {
            UmicomFatConsoleText(shell, "fat.transport=");
            UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(block));
            UmicomFatConsoleText(shell, "\r\n");
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        } else {
            /* Preserve a discoverable owner even if admission retains only a
             * transport handle requiring reset/release during shutdown. */
            umicomFatUpdateConsole = shell;
            status = UmicomKernelFat16UpdateOpen(&umicomFatConsoleUpdater, domain,
                slot, partition, 10000000U);
        }
        UmicomFatConsoleReport(shell, "fat.open", status);
        if (status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "Existing-file data updates enabled. Use fatwrite PATH OFFSET \"TEXT\", then fatflush.\r\n");
    } else if (write) {
        UmicomU64 offset = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &offset)) {
            umicomFatUpdateConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        const char *const input = command->bytes + command->offsets[3];
        UmicomSize bytes = 0U;
        while (input[bytes]) ++bytes;
        UmicomKernelFat16UpdateResult result;
        UmicomFatConsoleClear(&result, sizeof(result));
        /* This impossible request size identifies an untouched output record
         * after an owner/state/context refusal, preserving historical evidence. */
        result.requestedBytes = ~(UmicomSize)0U;
        status = UmicomKernelFat16UpdateWrite(&umicomFatConsoleUpdater,
            command->bytes + command->offsets[1], offset, input, bytes, &result);
        UmicomFatConsoleReport(shell, "fat.write", status);
        if (result.requestedBytes != ~(UmicomSize)0U)
            UmicomFatConsoleResult(shell, "fat.write-result", &result);
        else {
            UmicomFatConsoleText(shell, "fat.write-result=not-admitted; previous evidence retained\r\n");
            if (umicomFatConsoleUpdater.lastWrite.requestedBytes)
                UmicomFatConsoleResult(shell, "fat.previous-write", &umicomFatConsoleUpdater.lastWrite);
        }
        UmicomFatConsoleClear(&result, sizeof(result));
    } else if (flush) {
        UmicomKernelBlockMutationOutcome outcome = (UmicomKernelBlockMutationOutcome)3;
        status = UmicomKernelFat16UpdateFlush(&umicomFatConsoleUpdater, &outcome);
        UmicomFatConsoleReport(shell, "fat.flush", status);
        UmicomFatConsoleText(shell, "fat.flush-outcome=");
        UmicomFatConsoleText(shell, UmicomFatConsoleBlockOutcome(outcome));
        UmicomFatConsoleText(shell, "\r\n");
        if (umicomFatConsoleUpdater.writeUncertain)
            UmicomFatConsoleText(shell, "A flush cannot establish which bytes an earlier uncertain write changed.\r\n");
    } else if (close) {
        status = UmicomFatConsoleClose(shell);
        UmicomFatConsoleReport(shell, "fat.release", status);
    } else {
        UmicomFatConsoleReport(shell, "fat.status", umicomFatConsoleUpdater.lastStatus);
        if (umicomFatConsoleUpdater.lastWrite.requestedBytes)
            UmicomFatConsoleResult(shell, "fat.last-write", &umicomFatConsoleUpdater.lastWrite);
    }
    umicomFatUpdateConsoleBusy = UMICOM_FALSE;
    return status == UMICOM_FAT16_UPDATE_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}

/* The ordered updater owns a distinct console lifetime. Reuse the established
 * text/number/status formatting while retaining every original data-only
 * command. The new commands make the Stage/Finish persistence boundary visible;
 * shell teardown can release an unfinished owner but cannot silently finish it. */
#include "umicom/kernel/fat16_commit_console.h"
static UmicomKernelFat16Committer umicomFatConsoleCommitter;
static UmicomKernelConsoleShell *umicomFatCommitConsole;
static UmicomBoolean umicomFatCommitConsoleBusy;

static void UmicomFatCommitField(UmicomKernelConsoleShell *shell, const char *label, UmicomU64 value)
{
    UmicomFatConsoleText(shell, label);
    UmicomFatConsoleNumber(shell, value);
}
static void UmicomFatCommitState(UmicomKernelConsoleShell *shell)
{
    UmicomFatConsoleText(shell, "fat.commit-state=");
    UmicomFatConsoleText(shell, UmicomKernelFat16CommitStateName(umicomFatConsoleCommitter.state));
    UmicomFatCommitField(shell, " slot=", umicomFatConsoleCommitter.updater.slot);
    UmicomFatCommitField(shell, " partition=", umicomFatConsoleCommitter.updater.partition);
    UmicomFatCommitField(shell, " needs-flush=", umicomFatConsoleCommitter.updater.needsFlush ? 1U : 0U);
    UmicomFatCommitField(shell, " write-uncertain=", umicomFatConsoleCommitter.updater.writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static void UmicomFatCommitReport(UmicomKernelConsoleShell *shell, const char *operation,
    UmicomKernelFat16UpdateStatus status)
{
    UmicomFatConsoleText(shell, operation);
    UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(status));
    UmicomFatConsoleText(shell, " last-cleanup=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(umicomFatConsoleCommitter.updater.lastCleanupStatus));
    /* Failed Open has no operation result yet. Preserve exact admission
     * diagnostics so dirty media and inconsistent mirrors remain distinct. */
    UmicomFatConsoleText(shell, " last-disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(umicomFatConsoleCommitter.updater.lastDiskStatus));
    UmicomFatConsoleText(shell, " last-block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(umicomFatConsoleCommitter.updater.lastBlockStatus));
    UmicomFatConsoleText(shell, "\r\n");
    UmicomFatCommitState(shell);
}
static void UmicomFatCommitResult(UmicomKernelConsoleShell *shell, const char *label,
    const UmicomKernelFat16CommitResult *result)
{
    UmicomFatConsoleText(shell, label);
    UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(result->status));
    UmicomFatConsoleText(shell, " phase=");
    UmicomFatConsoleText(shell, UmicomKernelFat16CommitPhaseName(result->phase));
    UmicomFatConsoleText(shell, " disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(result->diskStatus));
    UmicomFatConsoleText(shell, " block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(result->blockStatus));
    UmicomFatConsoleText(shell, " last-mutation-outcome=");
    UmicomFatConsoleText(shell, UmicomFatConsoleBlockOutcome(result->lastBlockOutcome));
    UmicomFatConsoleText(shell, "\r\nfat.commit.data-outcome=");
    UmicomFatConsoleText(shell, UmicomFatConsoleOutcome(result->dataOutcome));
    UmicomFatCommitField(shell, " offset=", result->offset);
    UmicomFatCommitField(shell, " requested=", result->requestedBytes);
    UmicomFatCommitField(shell, " confirmed=", result->confirmedBytes);
    UmicomFatCommitField(shell, " submitted=", result->submittedBytes);
    UmicomFatCommitField(shell, " data-sectors-completed=", result->completedDataSectors);
    UmicomFatCommitField(shell, " data-sectors-submitted=", result->submittedDataSectors);
    UmicomFatCommitField(shell, " metadata-sectors-completed=", result->completedMetadataSectors);
    UmicomFatCommitField(shell, " metadata-sectors-submitted=", result->submittedMetadataSectors);
    UmicomFatCommitField(shell, " flushes-completed=", result->completedFlushes);
    UmicomFatConsoleText(shell, "\r\nfat.commit.observed");
    UmicomFatCommitField(shell, " dirty-durable=", result->dirtyDurable ? 1U : 0U);
    UmicomFatCommitField(shell, " dirty-verified=", result->dirtyVerified ? 1U : 0U);
    UmicomFatCommitField(shell, " data-durable=", result->dataDurable ? 1U : 0U);
    UmicomFatCommitField(shell, " data-verified=", result->dataVerified ? 1U : 0U);
    UmicomFatCommitField(shell, " clean-started=", result->cleanFinalisationStarted ? 1U : 0U);
    UmicomFatCommitField(shell, " clean-durable=", result->cleanDurable ? 1U : 0U);
    UmicomFatCommitField(shell, " clean-verified=", result->cleanVerified ? 1U : 0U);
    UmicomFatCommitField(shell, " commit-accepted=", result->commitAccepted ? 1U : 0U);
    UmicomFatCommitField(shell, " needs-flush-at-return=", result->needsFlush ? 1U : 0U);
    UmicomFatCommitField(shell, " write-uncertain-at-return=", result->writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
    if (result->uncertainSectorValid) {
        UmicomFatCommitField(shell, "fat.commit.uncertain-sector-lba=", result->uncertainSector);
        UmicomFatConsoleText(shell, " sector-bytes-at-risk=512\r\n");
    }
    if (result->cleanFinalisationStarted && !result->commitAccepted)
        UmicomFatConsoleText(shell, "Clean finalisation was submitted. A fresh reader may see a complete clean volume despite this error; do not retry automatically.\r\n");
}
static UmicomKernelFat16UpdateStatus UmicomFatCommitClose(UmicomKernelConsoleShell *shell)
{
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16CommitClose(&umicomFatConsoleCommitter);
    if (status == UMICOM_FAT16_UPDATE_OK && umicomFatConsoleCommitter.lastResult.mediaTouched &&
        !umicomFatConsoleCommitter.lastResult.commitAccepted)
        UmicomFatConsoleText(shell, "fat.commit.close=resources-released; update not accepted as committed; no flush or flag repair submitted\r\n");
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!shell || !shell->output) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!umicomFatCommitConsole) return UMICOM_FAT16_UPDATE_OK;
    if (umicomFatCommitConsole != shell) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (umicomFatCommitConsoleBusy) return UMICOM_FAT16_UPDATE_BUSY;
    umicomFatCommitConsoleBusy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomFatCommitClose(shell);
    umicomFatCommitConsoleBusy = UMICOM_FALSE;
    return status;
}
UmicomKernelShellStatus UmicomKernelFat16CommitCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *const name = command->bytes + command->offsets[0];
    const UmicomBoolean open = UmicomFatConsoleEqual(name, "fatcommitopen");
    const UmicomBoolean stage = UmicomFatConsoleEqual(name, "fatstage");
    const UmicomBoolean finish = UmicomFatConsoleEqual(name, "fatcommit");
    const UmicomBoolean info = UmicomFatConsoleEqual(name, "fatcommitinfo");
    const UmicomBoolean close = UmicomFatConsoleEqual(name, "fatcommitclose");
    if (!open && !stage && !finish && !info && !close) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    if (command->count != (open ? 3U : stage ? 4U : 1U)) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomFatCommitConsoleBusy) return UMICOM_SHELL_BUSY;
    if (umicomFatCommitConsole && umicomFatCommitConsole != shell) return UMICOM_SHELL_BAD_STATE;
    umicomFatCommitConsoleBusy = UMICOM_TRUE;
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    if (open) {
        UmicomU64 slot = 0U, partition = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
            !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
            slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS) {
            umicomFatCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
        const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
        if (block != UMICOM_BLOCK_OK) {
            UmicomFatConsoleText(shell, "fat.commit.transport=");
            UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(block));
            UmicomFatConsoleText(shell, "\r\n");
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        } else {
            umicomFatCommitConsole = shell;
            status = UmicomKernelFat16CommitOpen(&umicomFatConsoleCommitter, domain,
                slot, partition, 10000000U);
        }
        UmicomFatCommitReport(shell, "fat.commit.open", status);
        if (status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "Use fatstage PATH OFFSET \"TEXT\" to write and verify data with persistent dirty flags, then fatcommit to finish.\r\n");
    } else if (stage || finish) {
        UmicomU64 offset = 0U;
        if (stage && !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &offset)) {
            umicomFatCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        UmicomKernelFat16CommitResult result;
        UmicomFatConsoleClear(&result, sizeof(result));
        result.requestedBytes = ~(UmicomSize)0U;
        if (stage) {
            const char *const input = command->bytes + command->offsets[3];
            UmicomSize bytes = 0U;
            while (input[bytes]) ++bytes;
            status = UmicomKernelFat16CommitStage(&umicomFatConsoleCommitter,
                command->bytes + command->offsets[1], offset, input, bytes, &result);
        } else status = UmicomKernelFat16CommitFinish(&umicomFatConsoleCommitter, &result);
        UmicomFatCommitReport(shell, stage ? "fat.commit.stage" : "fat.commit.finish", status);
        if (result.requestedBytes != ~(UmicomSize)0U)
            UmicomFatCommitResult(shell, "fat.commit.result", &result);
        else {
            UmicomFatConsoleText(shell, "fat.commit.result=not-admitted; previous evidence retained\r\n");
            if (umicomFatConsoleCommitter.lastResult.requestedBytes)
                UmicomFatCommitResult(shell, "fat.commit.previous-result", &umicomFatConsoleCommitter.lastResult);
        }
        if (stage && status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "Data was flushed and verified. The volume remains dirty until fatcommit succeeds.\r\n");
        UmicomFatConsoleClear(&result, sizeof(result));
    } else if (close) {
        status = UmicomFatCommitClose(shell);
        UmicomFatCommitReport(shell, "fat.commit.release", status);
    } else {
        UmicomFatCommitReport(shell, "fat.commit.status", umicomFatConsoleCommitter.updater.lastStatus);
        if (umicomFatConsoleCommitter.lastResult.requestedBytes)
            UmicomFatCommitResult(shell, "fat.commit.last-result", &umicomFatConsoleCommitter.lastResult);
    }
    umicomFatCommitConsoleBusy = UMICOM_FALSE;
    return status == UMICOM_FAT16_UPDATE_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}

/* Explicit calendar metadata has a separate single-use console owner. Existing
 * data-only commands remain intact. No host time, RTC or elapsed Kernel ticks
 * is silently turned into a filesystem timestamp. */
#include "umicom/kernel/fat16_file_commit_console.h"
static UmicomKernelFat16FileCommitter umicomFatFileConsoleCommitter;
static UmicomKernelConsoleShell *umicomFatFileCommitConsole;
static UmicomBoolean umicomFatFileCommitConsoleBusy;
static UmicomKernelFat16FileTime umicomFatFileConsoleTime;
static UmicomBoolean umicomFatFileConsoleTimeSet;

static UmicomBoolean UmicomFatFileCalendar(const char *text, UmicomKernelFat16FileTime *time)
{
    /* Fixed-width syntax makes missing fields, suffixes, signs, spaces and
     * timezone claims unambiguous. Semantic Gregorian validation is shared
     * with the planner rather than approximated in the console. */
    static const UmicomSize positions[6] = {0U, 5U, 8U, 11U, 14U, 17U};
    static const UmicomSize widths[6] = {4U, 2U, 2U, 2U, 2U, 2U};
    UmicomSize length = 0U;
    while (length < 20U && text[length]) ++length;
    if (length != 19U || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') return UMICOM_FALSE;
    UmicomU16 fields[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    for (UmicomSize field = 0U; field < 6U; ++field) {
        for (UmicomSize digit = 0U; digit < widths[field]; ++digit) {
            const char value = text[positions[field] + digit];
            if (value < '0' || value > '9') return UMICOM_FALSE;
            fields[field] = (UmicomU16)((UmicomU32)fields[field] * 10U + (UmicomU32)(value - '0'));
        }
    }
    const UmicomKernelFat16FileTime staged = {fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]};
    UmicomKernelFat16FileTimeEncoding encoded;
    if (UmicomKernelFat16FileTimeEncode(&staged, &encoded) != UMICOM_DISK_OK) return UMICOM_FALSE;
    *time = staged;
    return UMICOM_TRUE;
}
static void UmicomFatFileDigits(UmicomKernelConsoleShell *shell, UmicomU16 value, UmicomSize width)
{
    char digits[4];
    for (UmicomSize i = width; i > 0U; --i) {
        digits[i - 1U] = (char)('0' + value % 10U);
        value = (UmicomU16)(value / 10U);
    }
    shell->output(shell->outputContext, digits, width);
}
static void UmicomFatFileTime(UmicomKernelConsoleShell *shell, const UmicomKernelFat16FileTime *time)
{
    UmicomFatFileDigits(shell, time->year, 4U); UmicomFatConsoleText(shell, "-");
    UmicomFatFileDigits(shell, time->month, 2U); UmicomFatConsoleText(shell, "-");
    UmicomFatFileDigits(shell, time->day, 2U); UmicomFatConsoleText(shell, "T");
    UmicomFatFileDigits(shell, time->hour, 2U); UmicomFatConsoleText(shell, ":");
    UmicomFatFileDigits(shell, time->minute, 2U); UmicomFatConsoleText(shell, ":");
    UmicomFatFileDigits(shell, time->second, 2U);
}
static void UmicomFatFileCommitReport(UmicomKernelConsoleShell *shell, const char *operation,
    UmicomKernelFat16UpdateStatus status)
{
    const UmicomKernelFat16Updater *const base = &umicomFatFileConsoleCommitter.commit.updater;
    UmicomFatConsoleText(shell, operation); UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(status));
    UmicomFatConsoleText(shell, " last-cleanup=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(base->lastCleanupStatus));
    UmicomFatConsoleText(shell, " last-disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(base->lastDiskStatus));
    UmicomFatConsoleText(shell, " last-block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(base->lastBlockStatus));
    UmicomFatConsoleText(shell, "\r\nfat.file-state=");
    UmicomFatConsoleText(shell, UmicomKernelFat16CommitStateName(umicomFatFileConsoleCommitter.commit.state));
    UmicomFatCommitField(shell, " slot=", base->slot);
    UmicomFatCommitField(shell, " partition=", base->partition);
    UmicomFatCommitField(shell, " needs-flush=", base->needsFlush ? 1U : 0U);
    UmicomFatCommitField(shell, " write-uncertain=", base->writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static void UmicomFatFileCommitResult(UmicomKernelConsoleShell *shell, const char *label,
    const UmicomKernelFat16FileCommitResult *result)
{
    /* The common lines are labelled fat.commit: their counters describe this
     * file result and include directory metadata as documented in the API. */
    UmicomFatCommitResult(shell, label, &result->commit);
    UmicomFatCommitField(shell, "fat.file.directory-planned=", result->directoryPlanned ? 1U : 0U);
    if (result->directoryPlanned) {
        UmicomFatCommitField(shell, " sector=", result->directorySector);
        UmicomFatCommitField(shell, " entry-offset=", result->entryOffset);
        UmicomFatCommitField(shell, " attributes-before=", result->originalAttributes);
        UmicomFatCommitField(shell, " attributes-after=", result->updatedAttributes);
        UmicomFatConsoleText(shell, "\r\nfat.file.requested-time=");
        UmicomFatFileTime(shell, &result->requestedTime);
        UmicomKernelFat16FileTime stored = result->requestedTime;
        stored.second = result->encodedTime.storedSecond;
        UmicomFatConsoleText(shell, " stored-time="); UmicomFatFileTime(shell, &stored);
        UmicomFatCommitField(shell, " fat-write-date=", result->encodedTime.writeDate);
        UmicomFatCommitField(shell, " fat-write-time=", result->encodedTime.writeTime);
    }
    UmicomFatConsoleText(shell, "\r\nfat.file.directory-observed");
    UmicomFatCommitField(shell, " submitted=", result->directorySubmitted ? 1U : 0U);
    UmicomFatCommitField(shell, " completed=", result->directoryCompleted ? 1U : 0U);
    UmicomFatCommitField(shell, " durable=", result->directoryDurable ? 1U : 0U);
    UmicomFatCommitField(shell, " verified=", result->directoryVerified ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static UmicomKernelFat16UpdateStatus UmicomFatFileCommitClose(UmicomKernelConsoleShell *shell)
{
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16FileCommitClose(&umicomFatFileConsoleCommitter);
    if (status == UMICOM_FAT16_UPDATE_OK && umicomFatFileConsoleCommitter.lastResult.commit.mediaTouched &&
        !umicomFatFileConsoleCommitter.lastResult.commit.commitAccepted)
        UmicomFatConsoleText(shell, "fat.file.close=resources-released; file commit not accepted; no flush or flag repair submitted\r\n");
    if (status == UMICOM_FAT16_UPDATE_OK) {
        UmicomFatConsoleClear(&umicomFatFileConsoleTime, sizeof(umicomFatFileConsoleTime));
        umicomFatFileConsoleTimeSet = UMICOM_FALSE;
    }
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomFatRenameConsoleClose(UmicomKernelConsoleShell *shell);
static UmicomBoolean UmicomFatRenameShellMatches(const UmicomKernelConsoleShell *shell);
static UmicomKernelShellStatus UmicomFatRenameCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!shell || !shell->output) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_FAT16_UPDATE_BUSY;
    /* The shared shell shutdown releases every private file-mutation owner.
     * Each Close remains resource-only; an unfinished rename stays dirty. */
    const UmicomKernelFat16UpdateStatus renameStatus = UmicomFatRenameConsoleClose(shell);
    if (renameStatus != UMICOM_FAT16_UPDATE_OK) return renameStatus;
    if (!umicomFatFileCommitConsole) return UMICOM_FAT16_UPDATE_OK;
    if (umicomFatFileCommitConsole != shell) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_FAT16_UPDATE_BUSY;
    umicomFatFileCommitConsoleBusy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomFatFileCommitClose(shell);
    umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
    return status;
}
static UmicomKernelShellStatus UmicomFatFileAppendCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command);
UmicomKernelShellStatus UmicomKernelFat16FileCommitCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *const name = command->bytes + command->offsets[0];
    UmicomBoolean renameHandled = UMICOM_FALSE;
    const UmicomKernelShellStatus renameStatus = UmicomFatRenameCommand(shell, command, &renameHandled);
    if (renameHandled) { *handled = UMICOM_TRUE; return renameStatus; }
    if (UmicomFatConsoleEqual(name, "fatfileappend")) {
        *handled = UMICOM_TRUE;
        return UmicomFatFileAppendCommand(shell, command);
    }
    const UmicomBoolean open = UmicomFatConsoleEqual(name, "fatfileopen");
    const UmicomBoolean calendar = UmicomFatConsoleEqual(name, "fatfiletime");
    const UmicomBoolean stage = UmicomFatConsoleEqual(name, "fatfilestage");
    const UmicomBoolean finish = UmicomFatConsoleEqual(name, "fatfilecommit");
    const UmicomBoolean info = UmicomFatConsoleEqual(name, "fatfileinfo");
    const UmicomBoolean close = UmicomFatConsoleEqual(name, "fatfileclose");
    if (!open && !calendar && !stage && !finish && !info && !close) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    if (command->count != (open ? 3U : calendar ? 2U : stage ? 4U : 1U)) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_SHELL_BUSY;
    if (!UmicomFatRenameShellMatches(shell)) return UMICOM_SHELL_BAD_STATE;
    if (umicomFatFileCommitConsole && umicomFatFileCommitConsole != shell) return UMICOM_SHELL_BAD_STATE;
    umicomFatFileCommitConsoleBusy = UMICOM_TRUE;
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    if (open) {
        UmicomU64 slot = 0U, partition = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
            !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
            slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS) {
            umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
        const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
        if (block != UMICOM_BLOCK_OK) {
            UmicomFatConsoleText(shell, "fat.file.transport=");
            UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(block));
            UmicomFatConsoleText(shell, "\r\n");
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        } else {
            umicomFatFileCommitConsole = shell;
            status = UmicomKernelFat16FileCommitOpen(&umicomFatFileConsoleCommitter, domain,
                slot, partition, 10000000U);
        }
        UmicomFatFileCommitReport(shell, "fat.file.open", status);
        if (status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "Use fatfiletime YYYY-MM-DDTHH:MM:SS, then fatfilestage PATH OFFSET \"TEXT\" and fatfilecommit. Seconds are floored to two-second precision; no timezone is inferred.\r\n");
    } else if (calendar) {
        UmicomKernelFat16FileTime time;
        UmicomFatConsoleClear(&time, sizeof(time));
        if (!UmicomFatFileCalendar(command->bytes + command->offsets[1], &time)) {
            UmicomFatConsoleText(shell, "fat.file.time=invalid-argument; expected a valid 1980..2107 calendar as YYYY-MM-DDTHH:MM:SS\r\n");
            umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        if (umicomFatFileConsoleCommitter.self != &umicomFatFileConsoleCommitter ||
            umicomFatFileConsoleCommitter.commit.state != UMICOM_FAT16_COMMIT_READY) {
            status = UMICOM_FAT16_UPDATE_BAD_STATE;
        } else {
            umicomFatFileConsoleTime = time;
            umicomFatFileConsoleTimeSet = UMICOM_TRUE;
        }
        UmicomFatFileCommitReport(shell, "fat.file.time", status);
        if (status == UMICOM_FAT16_UPDATE_OK) {
            UmicomFatConsoleText(shell, "fat.file.selected-time=");
            UmicomFatFileTime(shell, &time);
            time.second = (UmicomU16)((time.second / 2U) * 2U);
            UmicomFatConsoleText(shell, " stored-time=");
            UmicomFatFileTime(shell, &time);
            UmicomFatConsoleText(shell, "\r\n");
        }
        UmicomFatConsoleClear(&time, sizeof(time));
    } else if (stage || finish) {
        UmicomU64 offset = 0U;
        if (stage && !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &offset)) {
            umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        if (stage && !umicomFatFileConsoleTimeSet) {
            UmicomFatConsoleText(shell, "fat.file.stage=bad-state; select an explicit calendar with fatfiletime first; previous evidence retained\r\n");
            umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_IO_ERROR;
        }
        UmicomKernelFat16FileCommitResult result;
        UmicomFatConsoleClear(&result, sizeof(result));
        result.commit.requestedBytes = ~(UmicomSize)0U;
        if (stage) {
            const char *const input = command->bytes + command->offsets[3];
            UmicomSize bytes = 0U;
            while (input[bytes]) ++bytes;
            status = UmicomKernelFat16FileCommitStage(&umicomFatFileConsoleCommitter,
                command->bytes + command->offsets[1], offset, input, bytes, &umicomFatFileConsoleTime, &result);
        } else status = UmicomKernelFat16FileCommitFinish(&umicomFatFileConsoleCommitter, &result);
        UmicomFatFileCommitReport(shell, stage ? "fat.file.stage" : "fat.file.finish", status);
        if (result.commit.requestedBytes != ~(UmicomSize)0U)
            UmicomFatFileCommitResult(shell, "fat.file.result", &result);
        else {
            UmicomFatConsoleText(shell, "fat.file.result=not-admitted; previous evidence retained\r\n");
            if (umicomFatFileConsoleCommitter.lastResult.commit.requestedBytes)
                UmicomFatFileCommitResult(shell, "fat.file.previous-result", &umicomFatFileConsoleCommitter.lastResult);
        }
        if (stage && status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "File data and directory metadata were flushed and verified. The volume remains dirty until fatfilecommit succeeds.\r\n");
        UmicomFatConsoleClear(&result, sizeof(result));
    } else if (close) {
        status = UmicomFatFileCommitClose(shell);
        UmicomFatFileCommitReport(shell, "fat.file.release", status);
    } else {
        UmicomFatFileCommitReport(shell, "fat.file.status", umicomFatFileConsoleCommitter.commit.updater.lastStatus);
        if (umicomFatFileConsoleCommitter.lastResult.commit.requestedBytes)
            UmicomFatFileCommitResult(shell, "fat.file.last-result", &umicomFatFileConsoleCommitter.lastResult);
    }
    umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
    return status == UMICOM_FAT16_UPDATE_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}

/*-----------------------------------------------------------------------------
 * Dedicated metadata-only rename commands.
 *
 * The original file-command dispatcher owns the shared shell guard, including
 * output callbacks. A separate rename lifetime carries no calendar or data
 * update state and uses its own explicit Finish. Keeping these helpers in the
 * established provider translation unit preserves every live-shell link path.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_rename_commit.h"

static UmicomKernelFat16RenameCommitter umicomFatRenameConsoleCommitter;
static UmicomKernelConsoleShell *umicomFatRenameConsole;

static UmicomBoolean UmicomFatRenameShellMatches(const UmicomKernelConsoleShell *shell)
{
    /* Shared shutdown must never encounter two different shell identities.
     * Owners remain distinct, but both mutation families bind to one console. */
    return !umicomFatRenameConsole || umicomFatRenameConsole == shell;
}
static void UmicomFatRenameReport(UmicomKernelConsoleShell *shell, const char *operation,
    UmicomKernelFat16UpdateStatus status)
{
    const UmicomKernelFat16Updater *const base = &umicomFatRenameConsoleCommitter.commit.updater;
    UmicomFatConsoleText(shell, operation); UmicomFatConsoleText(shell, "=");
    UmicomFatConsoleText(shell, UmicomKernelFat16UpdateStatusName(status));
    UmicomFatConsoleText(shell, " last-cleanup=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(base->lastCleanupStatus));
    UmicomFatConsoleText(shell, " last-disk=");
    UmicomFatConsoleText(shell, UmicomKernelDiskStatusName(base->lastDiskStatus));
    UmicomFatConsoleText(shell, " last-block=");
    UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(base->lastBlockStatus));
    UmicomFatConsoleText(shell, "\r\nfat.rename-state=");
    UmicomFatConsoleText(shell, UmicomKernelFat16CommitStateName(umicomFatRenameConsoleCommitter.commit.state));
    UmicomFatCommitField(shell, " slot=", base->slot);
    UmicomFatCommitField(shell, " partition=", base->partition);
    UmicomFatCommitField(shell, " needs-flush=", base->needsFlush ? 1U : 0U);
    UmicomFatCommitField(shell, " write-uncertain=", base->writeUncertain ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static void UmicomFatRenameResult(UmicomKernelConsoleShell *shell, const char *label,
    const UmicomKernelFat16RenameResult *result)
{
    UmicomFatCommitResult(shell, label, &result->commit);
    UmicomFatCommitField(shell, "fat.rename.directory-planned=", result->directoryPlanned ? 1U : 0U);
    if (result->directoryPlanned) {
        UmicomFatCommitField(shell, " sector=", result->directorySector);
        UmicomFatCommitField(shell, " entry-offset=", result->entryOffset);
        UmicomFatConsoleText(shell, "\r\nfat.rename.original-name=");
        UmicomFatConsoleText(shell, result->originalEntry.name);
        UmicomFatConsoleText(shell, " updated-name=");
        UmicomFatConsoleText(shell, result->updatedName);
        UmicomFatCommitField(shell, " file-bytes=", result->originalEntry.bytes);
        UmicomFatCommitField(shell, " attributes=", result->originalEntry.attributes);
    }
    UmicomFatConsoleText(shell, "\r\nfat.rename.directory-observed");
    UmicomFatCommitField(shell, " submitted=", result->directorySubmitted ? 1U : 0U);
    UmicomFatCommitField(shell, " completed=", result->directoryCompleted ? 1U : 0U);
    UmicomFatCommitField(shell, " durable=", result->directoryDurable ? 1U : 0U);
    UmicomFatCommitField(shell, " verified=", result->directoryVerified ? 1U : 0U);
    UmicomFatConsoleText(shell, "\r\n");
}
static UmicomKernelFat16UpdateStatus UmicomFatRenameClose(UmicomKernelConsoleShell *shell)
{
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16RenameClose(&umicomFatRenameConsoleCommitter);
    if (status == UMICOM_FAT16_UPDATE_OK && umicomFatRenameConsoleCommitter.lastResult.commit.mediaTouched &&
        !umicomFatRenameConsoleCommitter.lastResult.commit.commitAccepted)
        UmicomFatConsoleText(shell, "fat.rename.close=resources-released; rename not accepted; no flush or flag repair submitted\r\n");
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomFatRenameConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!umicomFatRenameConsole) return UMICOM_FAT16_UPDATE_OK;
    if (umicomFatRenameConsole != shell) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_FAT16_UPDATE_BUSY;
    umicomFatFileCommitConsoleBusy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomFatRenameClose(shell);
    umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
    return status;
}
static UmicomKernelShellStatus UmicomFatRenameCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    const char *const name = command->bytes + command->offsets[0];
    const UmicomBoolean open = UmicomFatConsoleEqual(name, "fatrenameopen");
    const UmicomBoolean stage = UmicomFatConsoleEqual(name, "fatrenamestage");
    const UmicomBoolean finish = UmicomFatConsoleEqual(name, "fatrenamecommit");
    const UmicomBoolean info = UmicomFatConsoleEqual(name, "fatrenameinfo");
    const UmicomBoolean close = UmicomFatConsoleEqual(name, "fatrenameclose");
    *handled = open || stage || finish || info || close;
    if (!*handled) return UMICOM_SHELL_OK;
    if (command->count != (open || stage ? 3U : 1U)) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_SHELL_BUSY;
    if (umicomFatFileCommitConsole && umicomFatFileCommitConsole != shell) return UMICOM_SHELL_BAD_STATE;
    if (umicomFatRenameConsole && umicomFatRenameConsole != shell) return UMICOM_SHELL_BAD_STATE;
    umicomFatFileCommitConsoleBusy = UMICOM_TRUE;
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    if (open) {
        UmicomU64 slot = 0U, partition = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
            !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
            slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS) {
            umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
        const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
        if (block != UMICOM_BLOCK_OK) {
            UmicomFatConsoleText(shell, "fat.rename.transport=");
            UmicomFatConsoleText(shell, UmicomKernelBlockStatusName(block));
            UmicomFatConsoleText(shell, "\r\n");
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        } else {
            umicomFatRenameConsole = shell;
            status = UmicomKernelFat16RenameOpen(&umicomFatRenameConsoleCommitter,
                domain, slot, partition, 10000000U);
        }
        UmicomFatRenameReport(shell, "fat.rename.open", status);
        if (status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "Use fatrenamestage PATH NEWNAME, then fatrenamecommit. Names stay in the same directory; attributes, timestamps and file data are preserved.\r\n");
    } else if (stage || finish) {
        UmicomKernelFat16RenameResult result;
        UmicomFatConsoleClear(&result, sizeof(result));
        result.commit.requestedBytes = ~(UmicomSize)0U;
        if (stage)
            status = UmicomKernelFat16RenameStage(&umicomFatRenameConsoleCommitter,
                command->bytes + command->offsets[1], command->bytes + command->offsets[2], &result);
        else status = UmicomKernelFat16RenameFinish(&umicomFatRenameConsoleCommitter, &result);
        UmicomFatRenameReport(shell, stage ? "fat.rename.stage" : "fat.rename.finish", status);
        if (result.commit.requestedBytes != ~(UmicomSize)0U)
            UmicomFatRenameResult(shell, "fat.rename.result", &result);
        else {
            UmicomFatConsoleText(shell, "fat.rename.result=not-admitted; previous evidence retained\r\n");
            if (umicomFatRenameConsoleCommitter.lastResult.commit.phase != UMICOM_FAT16_COMMIT_NONE)
                UmicomFatRenameResult(shell, "fat.rename.previous-result", &umicomFatRenameConsoleCommitter.lastResult);
        }
        if (stage && status == UMICOM_FAT16_UPDATE_OK)
            UmicomFatConsoleText(shell, "The new short name was flushed and verified. The volume remains dirty until fatrenamecommit succeeds.\r\n");
        UmicomFatConsoleClear(&result, sizeof(result));
    } else if (close) {
        status = UmicomFatRenameClose(shell);
        UmicomFatRenameReport(shell, "fat.rename.release", status);
    } else {
        UmicomFatRenameReport(shell, "fat.rename.status", umicomFatRenameConsoleCommitter.commit.updater.lastStatus);
        if (umicomFatRenameConsoleCommitter.lastResult.commit.phase != UMICOM_FAT16_COMMIT_NONE)
            UmicomFatRenameResult(shell, "fat.rename.last-result", &umicomFatRenameConsoleCommitter.lastResult);
    }
    umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
    return status == UMICOM_FAT16_UPDATE_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}

/* Append belongs to the same single-use file-commit console lifetime. Keeping
 * the explicit calendar, Finish and Close commands avoids a second writable
 * owner or an implicit clean publication after adding file bytes. */
#include "umicom/kernel/fat16_file_append.h"
static UmicomKernelShellStatus UmicomFatFileAppendCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command)
{
    if (command->count != 3U) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomFatFileCommitConsoleBusy) return UMICOM_SHELL_BUSY;
    if (!UmicomFatRenameShellMatches(shell)) return UMICOM_SHELL_BAD_STATE;
    if (umicomFatFileCommitConsole && umicomFatFileCommitConsole != shell)
        return UMICOM_SHELL_BAD_STATE;
    umicomFatFileCommitConsoleBusy = UMICOM_TRUE;
    if (!umicomFatFileConsoleTimeSet) {
        UmicomFatConsoleText(shell, "fat.file.append=bad-state; select an explicit calendar with fatfiletime first; previous evidence retained\r\n");
        umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
        return UMICOM_SHELL_IO_ERROR;
    }
    /* The public command consumes the existing bounded parser's token copies.
     * Empty payloads retain the lower API's pre-admission refusal and history. */
    const char *const input = command->bytes + command->offsets[2];
    UmicomSize bytes = 0U;
    while (input[bytes]) ++bytes;
    UmicomKernelFat16FileCommitResult result;
    UmicomFatConsoleClear(&result, sizeof(result));
    result.commit.requestedBytes = ~(UmicomSize)0U;
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16FileCommitAppend(
        &umicomFatFileConsoleCommitter, command->bytes + command->offsets[1],
        input, bytes, &umicomFatFileConsoleTime, &result);
    UmicomFatFileCommitReport(shell, "fat.file.append", status);
    if (result.commit.requestedBytes != ~(UmicomSize)0U) {
        UmicomFatFileCommitResult(shell, "fat.file.result", &result);
        if (result.directoryPlanned) {
            /* These are planned sizes, even if a later write or barrier failed.
             * Durable and accepted evidence remains in the ordinary result. */
            UmicomFatCommitField(shell, "fat.file.append.original-bytes=", result.commit.offset);
            UmicomFatCommitField(shell, " planned-bytes=", result.commit.offset + result.commit.requestedBytes);
            UmicomFatConsoleText(shell, "\r\n");
        }
    } else {
        UmicomFatConsoleText(shell, "fat.file.result=not-admitted; previous evidence retained\r\n");
        if (umicomFatFileConsoleCommitter.lastResult.commit.requestedBytes)
            UmicomFatFileCommitResult(shell, "fat.file.previous-result", &umicomFatFileConsoleCommitter.lastResult);
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        UmicomFatConsoleText(shell, "Append bytes and the new file size were flushed and verified. The volume remains dirty until fatfilecommit succeeds.\r\n");
    UmicomFatConsoleClear(&result, sizeof(result));
    umicomFatFileCommitConsoleBusy = UMICOM_FALSE;
    return status == UMICOM_FAT16_UPDATE_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}
