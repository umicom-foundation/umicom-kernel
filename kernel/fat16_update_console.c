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
