/*
 * serialudp.c  --  read a Windows COM port and forward bytes to a UDP endpoint.
 *
 * Build (MinGW-w64):
 *     gcc serialudp.c -o serialudp.exe -lws2_32
 *
 * Usage:
 *     serialudp <COMx> <baud[,parity[,data[,stop]]]> <dst_ip> <dst_port> <logfile> <killfile>
 *
 * Example:
 *     serialudp COM3 115200,N,8,1 127.0.0.1 5000 C:\logs\bridge.log C:\tmp\stop.flag
 *
 * Config field:
 *     baud   : integer, e.g. 9600, 115200
 *     parity : N | E | O | M | S   (default N)
 *     data   : 5..8                (default 8)
 *     stop   : 1 | 1.5 | 2         (default 1)
 *
 * Termination: the program exits cleanly once <killfile> exists on disk,
 * or on Ctrl+C.
 */

#include <winsock2.h>   /* must precede windows.h */
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_BUF  4096
#define POLL_MS   100   /* max time a serial read blocks; also kill-file poll period */

static volatile LONG g_stop = 0;   /* set by Ctrl+C handler */

/* ---- logging -------------------------------------------------------------*/

static void logmsg(FILE *log, const char *fmt, ...)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    char stamp[32];
    _snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);

    /* to log file */
    fprintf(log, "%s  ", stamp);
    vfprintf(log, fmt, ap);
    fputc('\n', log);
    fflush(log);

    /* mirror to stderr for interactive use */
    va_end(ap);
    va_start(ap, fmt);
    fprintf(stderr, "%s  ", stamp);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    va_end(ap);
}

/* ---- ctrl+c --------------------------------------------------------------*/

static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;
    InterlockedExchange(&g_stop, 1);
    return TRUE;   /* handled; do not let default terminator kill us abruptly */
}

/* ---- config parsing ------------------------------------------------------*/

/* Fills a DCB from a "baud[,parity[,data[,stop]]]" string. Returns 0 on ok. */
static int parse_config(const char *cfg, DCB *dcb)
{
    char buf[64];
    strncpy(buf, cfg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* defaults */
    dcb->BaudRate = CBR_9600;
    dcb->Parity   = NOPARITY;
    dcb->ByteSize = 8;
    dcb->StopBits = ONESTOPBIT;

    char *save = NULL;
    char *tok  = strtok_r(buf, ",", &save);
    if (!tok) return -1;

    dcb->BaudRate = (DWORD)strtoul(tok, NULL, 10);
    if (dcb->BaudRate == 0) return -1;

    /* parity */
    if ((tok = strtok_r(NULL, ",", &save)) != NULL && tok[0]) {
        switch (tok[0]) {
            case 'N': case 'n': dcb->Parity = NOPARITY;    break;
            case 'E': case 'e': dcb->Parity = EVENPARITY;  break;
            case 'O': case 'o': dcb->Parity = ODDPARITY;   break;
            case 'M': case 'm': dcb->Parity = MARKPARITY;  break;
            case 'S': case 's': dcb->Parity = SPACEPARITY; break;
            default: return -1;
        }
    }

    /* data bits */
    if ((tok = strtok_r(NULL, ",", &save)) != NULL && tok[0]) {
        int d = atoi(tok);
        if (d < 5 || d > 8) return -1;
        dcb->ByteSize = (BYTE)d;
    }

    /* stop bits */
    if ((tok = strtok_r(NULL, ",", &save)) != NULL && tok[0]) {
        if      (strcmp(tok, "1")   == 0) dcb->StopBits = ONESTOPBIT;
        else if (strcmp(tok, "1.5") == 0) dcb->StopBits = ONE5STOPBITS;
        else if (strcmp(tok, "2")   == 0) dcb->StopBits = TWOSTOPBITS;
        else return -1;
    }

    return 0;
}

/* ---- serial open ---------------------------------------------------------*/

static HANDLE open_serial(const char *port, const char *cfg, FILE *log)
{
    /* The \\.\ prefix works for COM1..COM255, so always use it. */
    char path[64];
    _snprintf(path, sizeof(path), "\\\\.\\%s", port);

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        logmsg(log, "ERROR: cannot open %s (GetLastError=%lu)", port, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        logmsg(log, "ERROR: GetCommState failed (%lu)", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    if (parse_config(cfg, &dcb) != 0) {
        logmsg(log, "ERROR: bad config string '%s'", cfg);
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    dcb.fBinary = TRUE;
    dcb.fParity = (dcb.Parity != NOPARITY);

    if (!SetCommState(h, &dcb)) {
        logmsg(log, "ERROR: SetCommState failed (%lu)", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Bounded read: returns after POLL_MS even with no data, so the main loop
     * can poll the kill file ~10x/sec without a busy spin. */
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout        = 50;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant   = POLL_MS;
    if (!SetCommTimeouts(h, &to)) {
        logmsg(log, "ERROR: SetCommTimeouts failed (%lu)", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    logmsg(log, "opened %s baud=%lu parity=%d data=%d stop=%d",
           port, dcb.BaudRate, dcb.Parity, dcb.ByteSize, dcb.StopBits);
    return h;
}

/* ---- main ----------------------------------------------------------------*/

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr,
            "usage: %s <COMx> <baud[,parity[,data[,stop]]]> "
            "<dst_ip> <dst_port> <logfile> <killfile>\n"
            "example: %s COM3 115200,N,8,1 127.0.0.1 5000 bridge.log stop.flag\n",
            argv[0], argv[0]);
        return 2;
    }

    const char *port     = argv[1];
    const char *cfg      = argv[2];
    const char *dst_ip   = argv[3];
    const char *dst_port = argv[4];
    const char *logpath  = argv[5];
    const char *killpath = argv[6];

    FILE *log = fopen(logpath, "a");
    if (!log) {
        fprintf(stderr, "cannot open log file '%s'\n", logpath);
        return 2;
    }
    logmsg(log, "==== serialudp starting ====");

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    /* winsock */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logmsg(log, "ERROR: WSAStartup failed");
        fclose(log);
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        logmsg(log, "ERROR: socket() failed (%d)", WSAGetLastError());
        WSACleanup(); fclose(log);
        return 1;
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((unsigned short)atoi(dst_port));
    if (InetPtonA(AF_INET, dst_ip, &dst.sin_addr) != 1) {
        logmsg(log, "ERROR: bad destination IP '%s'", dst_ip);
        closesocket(sock); WSACleanup(); fclose(log);
        return 1;
    }
    logmsg(log, "forwarding to udp %s:%s", dst_ip, dst_port);

    HANDLE hcom = open_serial(port, cfg, log);
    if (hcom == INVALID_HANDLE_VALUE) {
        closesocket(sock); WSACleanup(); fclose(log);
        return 1;
    }

    /* main loop */
    unsigned char buf[READ_BUF];
    unsigned long long total = 0;
    const char *reason = "unknown";

    for (;;) {
        if (InterlockedCompareExchange(&g_stop, 0, 0)) {
            reason = "ctrl+c";
            break;
        }
        if (GetFileAttributesA(killpath) != INVALID_FILE_ATTRIBUTES) {
            reason = "kill file present";
            break;
        }

        DWORD nread = 0;
        if (!ReadFile(hcom, buf, sizeof(buf), &nread, NULL)) {
            logmsg(log, "ERROR: ReadFile failed (%lu)", GetLastError());
            reason = "serial read error";
            break;
        }
        if (nread == 0)
            continue;   /* timeout with no data; loop back to poll kill file */

        int sent = sendto(sock, (const char *)buf, (int)nread, 0,
                          (struct sockaddr *)&dst, sizeof(dst));
        if (sent == SOCKET_ERROR) {
            logmsg(log, "ERROR: sendto failed (%d)", WSAGetLastError());
            /* keep going; a transient UDP error should not kill the bridge */
            continue;
        }
        total += (unsigned long long)sent;
        logmsg(log, "forwarded %lu bytes (total %llu)", nread, total);
    }

    logmsg(log, "==== stopping: %s (forwarded %llu bytes) ====", reason, total);

    CloseHandle(hcom);
    closesocket(sock);
    WSACleanup();
    fclose(log);
    return 0;
}
