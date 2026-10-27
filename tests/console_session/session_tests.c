/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_session/session_tests.c
 *
 * PURPOSE:
 *   Exercise real serial input, RAM files and native program completion in QEMU.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

/* This Windows host fixture uses an ephemeral loopback socket for QEMU serial
 * bytes. It avoids CRT stdio translation and does not attach any host disk,
 * directory, guest network device or publicly reachable listener. Kernel
 * parser tests remain separate; this covers the transport-to-prompt boundary. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

enum { TRANSCRIPT_BYTES = 262144, COMMAND_BYTES = 8192 };
static char transcript[TRANSCRIPT_BYTES];

typedef struct ConsoleStep {
    const char *command;
    const char *reply;
} ConsoleStep;

/* The original RAM-only journey remains the default. A separately compiled
 * profile adds a read-only synthetic disk without changing the RAM contract. */
#ifndef UMICOM_CONSOLE_SESSION_STORAGE
static const ConsoleStep steps[] = {
    {"abi", "Compiled services describe this image, not granted permissions.\r\n"},
    {"abi extra", "unknown command or incorrect arguments; use help\r\n"},
    {"pwd", "\r\n/\r\n"},
    {"create /notes/lesson.txt", ""},
    {"write /notes/lesson.txt \"Kernel services use explicit requests\"", "\r\nwritten=37\r\n"},
    {"cat /notes/lesson.txt", "\r\nKernel services use explicit requests\r\n"},
    {"hex /notes/lesson.txt 7 8", "00000007  73 65 72 76 69 63 65 73                         |services|\r\nfile.hex offset=7 requested=8 read=8 next=15 eof=no\r\n"},
    {"hex /notes/lesson.txt 37 1", "file.hex offset=37 requested=1 read=0 next=37 eof=yes\r\n"},
    {"read /notes/lesson.txt 7 8", "services\r\nfile.read offset=7 requested=8 read=8 next=15 eof=no\r\n"},
    {"read /notes/lesson.txt 34 10", "sts\r\nfile.read offset=34 requested=10 read=3 next=37 eof=yes\r\n"},
    {"read /notes/lesson.txt 37 1", "file.read offset=37 requested=1 read=0 next=37 eof=yes\r\n"},
    {"read /notes/lesson.txt 0 4097", "read requires a decimal RAM file offset and 0..4096 bytes\r\n"},
    {"stat /notes/lesson.txt", " bytes=37\r\n"},
    {"append /notes/lesson.txt \".\"", "\r\nwritten=1\r\n"},
    {"cat /notes/lesson.txt", "\r\nKernel services use explicit requests.\r\n"},
    {"run /bin/umicom-diagnostic.elf 4", " calls=10\r\n"},
    {"poweroff", "RAM filesystem closed. Powering off.\r\nUMICOM_KERNEL_END\r\n"}
};

#else
static const ConsoleStep steps[] = {
    {"disks", "block.slot=0 base=0x10001000 probe=ok device-id=2 held-frames=0\r\n"},
    {"mountdisk 0", "unknown command or incorrect arguments; use help\r\n"},
    {"mountdisk nope 0", "unknown command or incorrect arguments; use help\r\n"},
    {"fatspace 0 0 extra", "unknown command or incorrect arguments; use help\r\n"},
    {"fatstat 0 0", "unknown command or incorrect arguments; use help\r\n"},
    {"fatspace 0 0", "disk.inspect=ok block=ok close=ok\r\n"},
    {"fatstat 0 0 /FRAG.BIN", "disk.inspect=ok block=ok close=ok\r\n"},
    {"fatcat 0 0 /README.TXT", "disk.inspect=ok block=ok close=ok\r\n"},
    {"mountdisk 0 0", "Read-only disk namespace mounted. Use diskls / and diskcat /README.TXT.\r\n"},
    /* Mounted operations report transport and cleanup status too. Match the
     * complete success contract; retain the shorter draft expectations. */
#if 0
    {"diskcat /README.TXT", "disk.read=ok\r\n"},
    {"unmountdisk", "disk.unmount=ok\r\n"},
#endif
    {"diskcat /README.TXT", "disk.read=ok block=ok cleanup=ok\r\n"},
    {"unmountdisk", "disk.unmount=ok block=ok cleanup=ok\r\n"},
    {"diskclose", "disk.close=ok\r\n"},
    {"poweroff", "RAM filesystem closed. Powering off.\r\nUMICOM_KERNEL_END\r\n"}
};
#endif

/* The fixture accepts explicit local ASCII paths, including spaces. Refusing
 * quotes/control bytes keeps this fixed CreateProcess argument vector exact.
 * This is not a general-purpose process launcher or a Unicode-path claim. */
static int PathAccepted(const char *text)
{
    if (text == NULL || text[0] == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p != 0U; ++p)
        if (*p < 32U || *p >= 127U || *p == '"') return 0;
    return 1;
}

static int Ready(SOCKET socket, int writing)
{
    fd_set selected;
    FD_ZERO(&selected);
    FD_SET(socket, &selected);
    struct timeval timeout = {0, 100000};
    return select(0, writing ? NULL : &selected, writing ? &selected : NULL,
        NULL, &timeout);
}

static int SendCommand(SOCKET socket, const char *command, ULONGLONG deadline)
{
    char line[256];
    const int count = snprintf(line, sizeof line, "%s\r", command);
    if (count < 0 || (size_t)count >= sizeof line) return 0;
    int sent = 0;
    while (sent < count && GetTickCount64() < deadline) {
        const int ready = Ready(socket, 1);
        if (ready < 0) return 0;
        if (ready == 0) continue;
        const int wrote = send(socket, line + sent, count - sent, 0);
        if (wrote == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
            return 0;
        }
        if (wrote == 0) return 0;
        sent += wrote;
    }
    return sent == count;
}

static int ResponseAccepted(size_t step, const char *text)
{
    if (strstr(text, steps[step].reply) == NULL) return 0;
    /* Match the command, not its array position, as new inspection steps are
     * added. Preserve the original positional check for review. */
#if 0
    if (step == 7U) {
#endif
    if (strcmp(steps[step].command, "fatspace 0 0") == 0) {
        if (strstr(text, "total-clusters=12159 free-clusters=12153 allocated-clusters=6 bad-clusters=0") == NULL ||
            strstr(text, "data-bytes=6225408 free-bytes=6222336 allocated-bytes=3072 bad-bytes=0") == NULL)
            return 0;
    }
    if (strcmp(steps[step].command, "fatstat 0 0 /FRAG.BIN") == 0 &&
        strstr(text, "disk.file.name=FRAG.BIN kind=file bytes=1300") == NULL) return 0;
    if ((strcmp(steps[step].command, "fatcat 0 0 /README.TXT") == 0 ||
         strcmp(steps[step].command, "diskcat /README.TXT") == 0) &&
        strstr(text, "These files are synthetic test data, not a host disk.") == NULL) return 0;
    if (strcmp(steps[step].command, "abi") == 0) {
        const char *required[] = {"abi.version=1.0\r\n", "abi.word-bits=64\r\n",
            "abi.byte-order=1\r\n", "abi.page-bytes=4096\r\n",
            "abi.copy-limit=64\r\n", "abi.call-limit=32\r\n", "abi.compiled-features="};
        for (size_t i = 0U; i < sizeof required / sizeof required[0]; ++i)
            if (strstr(text, required[i]) == NULL) return 0;
    }
    if (strcmp(steps[step].command, "run /bin/umicom-diagnostic.elf 4") == 0) {
        const char *report = strstr(text, "process id=");
        unsigned long long identity = 0U, value = 0U, slices = 0U, calls = 0U;
        if (report == NULL || sscanf(report,
                "process id=%llu state=exited exit=%llu slices=%llu calls=%llu",
                &identity, &value, &slices, &calls) != 4)
            return 0;
        /* The diagnostic computes identity + argument(4) + counter(33).
         * Do not confuse an orderly nonzero application result with QEMU's
         * own host exit status, which must still be zero after poweroff. */
        if (identity == 0U || identity > UINT64_MAX - 37U ||
            value != identity + 37U || slices == 0U || calls != 10U)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int result = 1;
    WSADATA sockets;
    SOCKET listener = INVALID_SOCKET, connection = INVALID_SOCKET;
    PROCESS_INFORMATION child = {0};
    int started = 0;
    size_t used = 0U, responseStart = 0U, sent = 0U;
    int finished = 0;
    const size_t stepCount = sizeof steps / sizeof steps[0];
#ifdef UMICOM_CONSOLE_SESSION_STORAGE
    /* Commas delimit QEMU drive properties. Reject them in this fixed fixture
     * argument instead of allowing a path to inject another drive setting. */
    if (argc != 4 || !PathAccepted(argv[1]) || !PathAccepted(argv[2]) ||
        !PathAccepted(argv[3]) || strchr(argv[3], ',') != NULL ||
        GetFileAttributesA(argv[3]) == INVALID_FILE_ATTRIBUTES ||
        (GetFileAttributesA(argv[3]) & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
        fprintf(stderr, "Usage: umicom-console-storage-session-tests QEMU_EXE CONSOLE_ELF SYNTHETIC_RAW_IMAGE\n");
        return 2;
    }
#else
    if (argc != 3 || !PathAccepted(argv[1]) || !PathAccepted(argv[2])) {
        fprintf(stderr, "Usage: umicom-console-session-tests QEMU_EXE CONSOLE_ELF\n");
        return 2;
    }
#endif
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) return 3;
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) goto cleanup;
    /* Bind only loopback and ask Windows for an unused port. Keep the listener
     * open from allocation through accept; no discover-close-rebind race. */
    BOOL exclusive = TRUE;
    if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            (const char *)&exclusive, (int)sizeof exclusive) != 0) goto cleanup;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0U;
    if (bind(listener, (const struct sockaddr *)&address, (int)sizeof address) != 0 ||
        listen(listener, 1) != 0) goto cleanup;
    int addressBytes = (int)sizeof address;
    if (getsockname(listener, (struct sockaddr *)&address, &addressBytes) != 0)
        goto cleanup;
    char command[COMMAND_BYTES];
    const int count = snprintf(command, sizeof command,
        "\"%s\" -machine virt,aclint=off -bios \"%s\" -display none -monitor none "
        "-chardev socket,id=serial,host=127.0.0.1,port=%u -serial chardev:serial "
        "-m 128M -smp 1 -no-reboot",
        argv[1], argv[2], (unsigned)ntohs(address.sin_port));
    if (count < 0 || (size_t)count >= sizeof command) goto cleanup;
#ifdef UMICOM_CONSOLE_SESSION_STORAGE
    const size_t remaining = sizeof command - (size_t)count;
    const int diskCount = snprintf(command + count, remaining,
        " -global virtio-mmio.force-legacy=false"
        " -drive \"file=%s,if=none,format=raw,id=lesson,readonly=on\""
        /* Pin the fixture to Kernel slot zero. QEMU's default auto-placement
         * selects the highest free bus, so it cannot define our CLI contract.
         * Keep the earlier implicit placement disabled for review. */
#if 0
        " -device virtio-blk-device,drive=lesson",
#else
        " -device virtio-blk-device,drive=lesson,bus=virtio-mmio-bus.0",
#endif
        argv[3]);
    if (diskCount < 0 || (size_t)diskCount >= remaining) goto cleanup;
#endif
    STARTUPINFOA startup = {0};
    startup.cb = sizeof startup;
    if (!CreateProcessA(argv[1], command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
            NULL, NULL, &startup, &child)) {
        fprintf(stderr, "QEMU could not start; Windows error %lu\n", GetLastError());
        goto cleanup;
    }
    started = 1;
    CloseHandle(child.hThread);
    child.hThread = NULL;
    const ULONGLONG deadline = GetTickCount64() + 60000U;
    while (GetTickCount64() < deadline && connection == INVALID_SOCKET) {
        const int ready = Ready(listener, 0);
        if (ready < 0) goto cleanup;
        if (ready > 0) connection = accept(listener, NULL, NULL);
        if (WaitForSingleObject(child.hProcess, 0U) == WAIT_OBJECT_0 &&
            connection == INVALID_SOCKET) {
            fprintf(stderr, "QEMU exited before connecting its serial channel.\n");
            goto cleanup;
        }
    }
    if (connection == INVALID_SOCKET) goto cleanup;
    u_long nonblocking = 1U;
    /* Winsock declares the request as signed long while the Windows headers
     * express FIONBIO's bit pattern as unsigned. Name that ABI conversion
     * explicitly; retain the implicit conversion for diagnostic review. */
#if 0
    if (ioctlsocket(connection, FIONBIO, &nonblocking) != 0) goto cleanup;
#endif
    if (ioctlsocket(connection, (long)FIONBIO, &nonblocking) != 0) goto cleanup;
    while (GetTickCount64() < deadline && !finished) {
        const int ready = Ready(connection, 0);
        if (ready < 0) goto cleanup;
        if (ready == 0) continue;
        char chunk[4096];
        const int received = recv(connection, chunk, (int)sizeof chunk, 0);
        if (received <= 0) {
            const int error = received == SOCKET_ERROR ? WSAGetLastError() : 0;
            if (error == WSAEWOULDBLOCK) continue;
            /* A Windows QEMU may reset serial on orderly exit. Only the
             * completed final response can make that transport closure valid. */
            if (sent == stepCount && ResponseAccepted(stepCount - 1U,
                    transcript + responseStart)) {
                finished = 1;
                break;
            }
            fprintf(stderr, "Serial closed before completion (error %d).\n", error);
            goto cleanup;
        }
        if (memchr(chunk, 0, (size_t)received) != NULL ||
            (size_t)received >= sizeof transcript - used) {
            fprintf(stderr, "Invalid or oversized serial transcript.\n");
            goto cleanup;
        }
        memcpy(transcript + used, chunk, (size_t)received);
        used += (size_t)received;
        transcript[used] = '\0';
        if (fwrite(chunk, 1U, (size_t)received, stdout) != (size_t)received)
            goto cleanup;
        if (strstr(transcript, "UMICOM_KERNEL_FAIL") != NULL) goto cleanup;
        char *prompt = strstr(transcript + responseStart, "umicom> ");
        if (prompt != NULL && sent < stepCount) {
            const char saved = *prompt;
            *prompt = '\0';
            const int accepted = sent == 0U
                ? strstr(transcript, "UMICOM_KERNEL_CONSOLE_READY") != NULL
                : ResponseAccepted(sent - 1U, transcript + responseStart);
            *prompt = saved;
            if (!accepted) {
                fprintf(stderr, "Unexpected response before step %zu.\n", sent);
                goto cleanup;
            }
            responseStart = (size_t)(prompt - transcript) + strlen("umicom> ");
            if (!SendCommand(connection, steps[sent].command, deadline)) goto cleanup;
            ++sent;
        }
        if (sent == stepCount && ResponseAccepted(stepCount - 1U,
                transcript + responseStart)) finished = 1;
    }
    if (!finished) {
        fprintf(stderr, "Serial session timed out after %zu commands.\n", sent);
        goto cleanup;
    }
    if (WaitForSingleObject(child.hProcess, 5000U) != WAIT_OBJECT_0) goto cleanup;
    DWORD exitCode = 1U;
    if (!GetExitCodeProcess(child.hProcess, &exitCode) || exitCode != 0U) {
        fprintf(stderr, "QEMU exit was not successful: %lu\n", exitCode);
        goto cleanup;
    }
    puts("UMICOM_KERNEL_SERIAL_SESSION_PASS");
    result = 0;
cleanup:
    if (connection != INVALID_SOCKET) closesocket(connection);
    if (listener != INVALID_SOCKET) closesocket(listener);
    /* A failing fixture owns this one QEMU process only. It never enumerates,
     * terminates or changes another build, console session or repository. */
    if (started) {
        if (WaitForSingleObject(child.hProcess, 0U) == WAIT_TIMEOUT) {
            (void)TerminateProcess(child.hProcess, 1U);
            (void)WaitForSingleObject(child.hProcess, 5000U);
        }
        CloseHandle(child.hProcess);
    }
    WSACleanup();
    return result;
}
