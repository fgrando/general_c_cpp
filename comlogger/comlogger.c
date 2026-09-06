/*
 * comlogger.c -- serial port logger for lab / CI use. See PROG_NAME/
 * PROG_VERSION below for the tool's name and version; both are used at
 * runtime (--help, --version, log messages), so change them there rather
 * than in this comment.
 *
 * Reads a COM port and appends everything to a text file. The file is closed
 * and reopened on every flush (default 500 ms) so Notepad, tail viewers and
 * editors that take an exclusive read can always open it mid-capture.
 *
 * Stops gracefully when a kill file appears, on Ctrl-C / console close, or
 * when --timeout expires, so a Jenkins step can start it in the background
 * and stop it by touching a file -- no PID juggling, no taskkill.
 *
 * Usage:
 *   comlogger --port COM3,115200,N,8,1 --out uart.log --kill uart.stop \
 *             [--flow none|rtscts|xonxoff] [--append] [--timestamp] [--echo]
 *             [--flush-ms 500] [--no-reconnect] [--quiet] [--record base]
 *             [--timeout seconds]
 *   comlogger --replay base --out uart.log [--kill uart.stop] [--timestamp]
 *             [--echo] [--flush-ms 500] [--append] [--quiet] [--timeout s]
 *
 *   --port takes COM_PORT[,BAUD[,PARITY[,DATABITS[,STOPBITS]]]]; trailing
 *   fields may be omitted (defaults: 115200,N,8,1).
 *
 *   --record base   alongside a live capture, also writes base.raw (exact
 *                   bytes off the wire) and base.timing (offset_ms/length
 *                   per chunk), so the run can be reproduced later.
 *   --replay base   feeds a --record capture back into --out at the
 *                   original pacing, with no COM port involved -- for
 *                   reproducing a test case without the device attached.
 *   --timeout s     stop after s seconds even if no kill file arrives and
 *                   no Ctrl-C is sent -- a watchdog for an unattended run.
 *   comlogger --list       list COM ports present on this machine
 *   comlogger --version    print name and version
 *
 * Exit codes: 0 stopped cleanly            2 usage error
 *             3 port/capture never opened  4 out of memory buffering output
 *             5 read failed and --no-reconnect was given
 *             6 --timeout expired with no kill file or Ctrl-C
 */

#ifdef _WIN32
#include <windows.h>
#endif
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Change these two to rename or version the tool; every runtime message
 * (usage, --version, "prog: ..." errors) is derived from them. */
#define PROG_NAME    "comlogger"
#define PROG_VERSION "1.0.0"

/* ------------------------------------------------------------------ config */

static const char *o_port    = NULL;
static const char *o_out     = NULL;
static const char *o_kill    = NULL;
static int   o_baud          = 115200;
static int   o_data          = 8;
static char  o_parity        = 'N';
static const char *o_stop    = "1";
static const char *o_flow    = "none";
static int   o_append        = 0;
static int   o_timestamp     = 0;
static int   o_echo          = 0;
static int   o_flush_ms      = 500;
static int   o_reconnect     = 1;
static int   o_quiet         = 0;
static const char *o_record  = NULL;
static const char *o_replay  = NULL;
static int   o_timeout_sec   = 0;      /* 0 = disabled */

/* --------------------------------------------------------- output assembly
 *
 * Kept free of Windows calls so it can be unit-tested on any host: the only
 * environment-dependent input is the timestamp, which is injected.
 */

typedef struct {
    char  *buf;
    size_t len, cap;
    int    at_line_start;
} obuf_t;

static int ob_reserve(obuf_t *o, size_t extra)
{
    if (o->len + extra <= o->cap) return 1;
    size_t want = o->cap ? o->cap : 8192;
    while (want < o->len + extra) want *= 2;
    char *p = (char *)realloc(o->buf, want);
    if (!p) return 0;
    o->buf = p; o->cap = want;
    return 1;
}

static int ob_put(obuf_t *o, const char *s, size_t n)
{
    if (!ob_reserve(o, n)) return 0;
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    return 1;
}

/*
 * Append a chunk, inserting `stamp` at the start of every line when stamping.
 *
 * The at_line_start flag lives across calls on purpose: serial data arrives in
 * arbitrary chunks, and a line split across two reads must not get a second
 * timestamp welded into its middle.
 */
static int ob_append_chunk(obuf_t *o, const char *data, size_t n,
                           const char *stamp)
{
    size_t i = 0, start;
    if (!stamp) return ob_put(o, data, n);

    while (i < n) {
        if (o->at_line_start) {
            if (!ob_put(o, stamp, strlen(stamp))) return 0;
            o->at_line_start = 0;
        }
        start = i;
        while (i < n && data[i] != '\n') i++;
        if (i < n) {                       /* include the newline itself */
            i++;
            o->at_line_start = 1;
        }
        if (!ob_put(o, data + start, i - start)) return 0;
    }
    return 1;
}

static void ob_reset(obuf_t *o) { o->len = 0; }
static void ob_free(obuf_t *o) { free(o->buf); o->buf = NULL; o->len = o->cap = 0; }

/* ============================ Windows only ============================ */
#ifdef _WIN32

static volatile LONG g_stop = 0;

/* The open COM port, if any. Global so crash_handler() can reach it -- see
 * there for why TerminateProcess needs no such handling but an in-process
 * crash does. */
static HANDLE g_port = INVALID_HANDLE_VALUE;

static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;                 /* CTRL_C, CLOSE, LOGOFF, SHUTDOWN alike */
    InterlockedExchange(&g_stop, 1);
    /* Returning TRUE means "handled": the main loop gets to flush and close
     * instead of the process being torn down mid-write. */
    return TRUE;
}

/*
 * A hard kill (Task Manager "End task", `taskkill /F`, TerminateProcess)
 * never runs any of our code -- Windows closes every handle a terminated
 * process held, COM port included, on its own, so the port is never left
 * "stuck" by that path. What *isn't* covered is an in-process crash (e.g. an
 * access violation): that unwinds past ctrl_handler and everything else,
 * so this filter gets one last chance to release the port before the
 * default crash handling (and possibly a Watson/WER dialog) takes over.
 */
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    if (g_port != INVALID_HANDLE_VALUE) CloseHandle(g_port);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* --timeout watchdog: an absolute GetTickCount() deadline set once at
 * startup, so a run with no kill file and no Ctrl-C still terminates. */
static DWORD g_deadline;
static int   g_have_deadline = 0;

static int timeout_hit(void)
{
    return g_have_deadline && (LONG)(GetTickCount() - g_deadline) >= 0;
}

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    if (o_quiet) return;
    va_start(ap, fmt);
    fprintf(stderr, PROG_NAME ": ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

static void now_stamp(char *out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out, n, "[%02u:%02u:%02u.%03u] ",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static int file_exists(const char *p)
{
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

/* -------------------------------------------------------------- COM ports */

static void list_ports(void)
{
    char *buf = (char *)malloc(65536), *p;
    DWORD n;
    if (!buf) return;
    n = QueryDosDeviceA(NULL, buf, 65536);
    if (!n) { free(buf); logmsg("QueryDosDevice failed (%lu)", GetLastError()); return; }
    for (p = buf; *p; p += strlen(p) + 1)
        if (!strncmp(p, "COM", 3) && p[3] >= '0' && p[3] <= '9')
            printf("%s\n", p);
    free(buf);
}

static HANDLE open_port(void)
{
    char path[64];
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS to;

    /* The \\.\ form is required for COM10 and above; harmless below it. */
    snprintf(path, sizeof(path), "\\\\.\\%s", o_port);

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    SetupComm(h, 1 << 16, 1 << 12);

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }

    dcb.BaudRate = (DWORD)o_baud;
    dcb.ByteSize = (BYTE)o_data;
    dcb.fBinary = TRUE;
    switch (o_parity) {
    case 'E': dcb.Parity = EVENPARITY; break;
    case 'O': dcb.Parity = ODDPARITY;  break;
    case 'M': dcb.Parity = MARKPARITY; break;
    case 'S': dcb.Parity = SPACEPARITY; break;
    default:  dcb.Parity = NOPARITY;   break;
    }
    dcb.fParity = (dcb.Parity != NOPARITY);
    if (!strcmp(o_stop, "2"))        dcb.StopBits = TWOSTOPBITS;
    else if (!strcmp(o_stop, "1.5")) dcb.StopBits = ONE5STOPBITS;
    else                             dcb.StopBits = ONESTOPBIT;

    dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE; dcb.fInX = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!strcmp(o_flow, "rtscts")) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else if (!strcmp(o_flow, "xonxoff")) {
        dcb.fOutX = TRUE; dcb.fInX = TRUE;
        dcb.XonChar = 17; dcb.XoffChar = 19;
        dcb.XonLim = 2048; dcb.XoffLim = 2048;
    }

    if (!SetCommState(h, &dcb)) { CloseHandle(h); return INVALID_HANDLE_VALUE; }

    /* Documented idiom: interval MAXDWORD + multiplier MAXDWORD + a nonzero
     * constant means "return buffered bytes at once, else wait up to constant
     * for the first byte". Gives a ~200 ms loop tick with no busy-waiting. */
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = MAXDWORD;
    to.ReadTotalTimeoutConstant    = 200;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

/* ------------------------------------------------------------------ output */

/* Open, append, flush to disk, close. Closing is the point: some editors take
 * an exclusive read share, and a handle we hold open would lock them out. */
static int flush_to_file(obuf_t *o)
{
    HANDLE f;
    DWORD written = 0;
    if (o->len == 0) return 1;

    f = CreateFileA(o_out, FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        logmsg("WARNING: cannot open %s (%lu); keeping %u bytes buffered",
               o_out, GetLastError(), (unsigned)o->len);
        return 0;                       /* keep the data, retry next tick */
    }
    if (!WriteFile(f, o->buf, (DWORD)o->len, &written, NULL) || written != o->len) {
        logmsg("WARNING: short write to %s (%lu of %u)",
               o_out, written, (unsigned)o->len);
        CloseHandle(f);
        return 0;
    }
    FlushFileBuffers(f);
    CloseHandle(f);
    ob_reset(o);
    return 1;
}

/* -------------------------------------------------------------- recording */

static FILE *g_rec_raw = NULL, *g_rec_timing = NULL;
static DWORD g_rec_t0;

/*
 * --record's sidecar capture: base.raw is an exact byte-for-byte copy of
 * what came off the wire (o_out isn't safe to replay from -- --timestamp
 * interleaves text into it), and base.timing is one "offset_ms length" line
 * per chunk read, so --replay can reproduce both the bytes and the pacing.
 */
static int record_open(void)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s.raw", o_record);
    g_rec_raw = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s.timing", o_record);
    g_rec_timing = fopen(path, "w");
    if (!g_rec_raw || !g_rec_timing) return 0;
    g_rec_t0 = GetTickCount();
    return 1;
}

static void record_chunk(const char *data, DWORD len)
{
    fwrite(data, 1, len, g_rec_raw);
    fprintf(g_rec_timing, "%lu %lu\n",
            (unsigned long)(GetTickCount() - g_rec_t0), (unsigned long)len);
}

static void record_close(void)
{
    if (g_rec_raw)    { fclose(g_rec_raw);    g_rec_raw = NULL; }
    if (g_rec_timing) { fclose(g_rec_timing); g_rec_timing = NULL; }
}

/* ------------------------------------------------------------------- args */

static void usage(void)
{
    fprintf(stderr,
PROG_NAME " -- log a serial port to a file that stays readable while capturing\n\n"
"  " PROG_NAME " --port COM3,115200,N,8,1 --out uart.log [--kill uart.stop]\n"
"         [--flow none|rtscts|xonxoff] [--append] [--timestamp] [--echo]\n"
"         [--flush-ms 500] [--no-reconnect] [--quiet] [--record base]\n"
"         [--timeout seconds]\n\n"
"  --port COM_PORT[,BAUD[,PARITY[,DATABITS[,STOPBITS]]]]\n"
"         defaults for omitted fields: 115200,N,8,1\n"
"         e.g. --port COM3  or  --port COM3,9600,E,7,2\n\n"
"  --record base   also write base.raw + base.timing for later --replay\n"
"  --timeout s     stop after s seconds even with no kill file or Ctrl-C\n\n"
"  " PROG_NAME " --replay base --out uart.log [--kill uart.stop] [--timestamp]\n"
"         [--echo] [--flush-ms 500] [--append] [--quiet] [--timeout s]\n"
"         replay a --record capture into --out at its original pacing,\n"
"         with no COM port involved\n\n"
"  " PROG_NAME " --list          list COM ports present on this machine\n"
"  " PROG_NAME " --version       print name and version\n\n"
"Stops cleanly when the kill file appears, on Ctrl-C / console close, or\n"
"when --timeout expires.\n");
}

static void need(int i, int argc, const char *what)
{
    if (i >= argc) { fprintf(stderr, PROG_NAME ": %s needs a value\n", what); exit(2); }
}

/* Splits "COM3,115200,N,8,1" in place on commas: port,baud,parity,data,stop.
 * Trailing fields are optional and leave the default intact, so "--port COM3"
 * alone still works. */
static void parse_port_spec(char *spec)
{
    char *tok = strtok(spec, ",");
    if (!tok || !*tok) { fprintf(stderr, PROG_NAME ": --port needs a COM port name\n"); exit(2); }
    o_port = tok;
    if ((tok = strtok(NULL, ",")) != NULL && *tok) o_baud   = atoi(tok);
    if ((tok = strtok(NULL, ",")) != NULL && *tok) o_parity = (char)toupper((unsigned char)tok[0]);
    if ((tok = strtok(NULL, ",")) != NULL && *tok) o_data   = atoi(tok);
    if ((tok = strtok(NULL, ",")) != NULL && *tok) o_stop   = tok;
}

/* Sleeps up to ms milliseconds in short steps so a kill file or Ctrl-C is
 * noticed within ~100 ms even during a long gap between replayed chunks.
 * Returns 0 if the wait was cut short, 1 if it ran to completion. */
static int replay_sleep(DWORD ms)
{
    while (ms > 0) {
        DWORD step = ms > 100 ? 100 : ms;
        if (g_stop || timeout_hit() || (o_kill && file_exists(o_kill))) return 0;
        Sleep(step);
        ms -= step;
    }
    return !g_stop && !timeout_hit() && !(o_kill && file_exists(o_kill));
}

/*
 * Feeds a --record capture back through the same assembly/flush pipeline
 * used for live capture, at the original inter-chunk pacing, so a
 * downstream watcher can be exercised against a reproducible test case with
 * no device attached.
 */
static int run_replay(void)
{
    char path[MAX_PATH], line[64], stamp[32];
    FILE *raw, *timing;
    obuf_t ob;
    char *buf = NULL;
    size_t cap = 0;
    unsigned long offset, len, prev = 0;
    DWORD last_flush;
    unsigned long long total = 0;
    int rc = 0;

    snprintf(path, sizeof(path), "%s.raw", o_replay);
    raw = fopen(path, "rb");
    snprintf(path, sizeof(path), "%s.timing", o_replay);
    timing = fopen(path, "r");
    if (!raw || !timing) {
        fprintf(stderr, PROG_NAME ": cannot open replay capture %s.raw/.timing\n", o_replay);
        if (raw) fclose(raw);
        if (timing) fclose(timing);
        return 3;
    }

    if (!o_append) {
        HANDLE t = CreateFileA(o_out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
    }

    memset(&ob, 0, sizeof(ob));
    ob.at_line_start = 1;
    last_flush = GetTickCount();
    logmsg("replaying %s.{raw,timing} into %s", o_replay, o_out);

    while (fgets(line, sizeof(line), timing)) {
        DWORD now, wait;

        if (timeout_hit()) { rc = 6; break; }
        if (sscanf(line, "%lu %lu", &offset, &len) != 2) continue;
        wait = (offset > prev) ? (DWORD)(offset - prev) : 0;
        prev = offset;
        if (!replay_sleep(wait)) { if (timeout_hit()) rc = 6; break; }

        if (len > cap) {
            char *p = (char *)realloc(buf, len);
            if (!p) { logmsg("out of memory replaying %lu bytes", len); rc = 4; break; }
            buf = p; cap = len;
        }
        if (fread(buf, 1, len, raw) != len) {
            logmsg("replay capture %s.raw is shorter than %s.timing says", o_replay, o_replay);
            break;
        }

        if (o_timestamp) now_stamp(stamp, sizeof(stamp));
        if (!ob_append_chunk(&ob, buf, len, o_timestamp ? stamp : NULL)) {
            logmsg("out of memory buffering %lu bytes", len);
            rc = 4; break;
        }
        total += len;
        if (o_echo) { fwrite(buf, 1, len, stdout); fflush(stdout); }

        now = GetTickCount();
        if ((DWORD)(now - last_flush) >= (DWORD)o_flush_ms) {
            flush_to_file(&ob);
            last_flush = now;
        }
    }
    if (g_stop) logmsg("interrupted, stopping replay");
    else if (timeout_hit()) logmsg("timeout of %ds reached, stopping replay", o_timeout_sec);
    else if (o_kill && file_exists(o_kill)) logmsg("kill file %s present, stopping replay", o_kill);

    flush_to_file(&ob);
    ob_free(&ob);
    free(buf);
    fclose(raw);
    fclose(timing);
    if (o_kill && file_exists(o_kill)) DeleteFileA(o_kill);
    logmsg("replay finished after %llu bytes", total);
    return rc;
}

int main(int argc, char **argv)
{
    obuf_t ob;
    char chunk[8192], stamp[32];
    DWORD last_flush, now, got;
    unsigned long long total = 0;
    int i, rc = 0;

    memset(&ob, 0, sizeof(ob));
    ob.at_line_start = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--list"))            { list_ports(); return 0; }
        else if (!strcmp(a, "--port"))       { need(++i, argc, a); parse_port_spec(argv[i]); }
        else if (!strcmp(a, "--flow"))       { need(++i, argc, a); o_flow = argv[i]; }
        else if (!strcmp(a, "--out"))        { need(++i, argc, a); o_out = argv[i]; }
        else if (!strcmp(a, "--kill"))       { need(++i, argc, a); o_kill = argv[i]; }
        else if (!strcmp(a, "--flush-ms"))   { need(++i, argc, a); o_flush_ms = atoi(argv[i]); }
        else if (!strcmp(a, "--record"))     { need(++i, argc, a); o_record = argv[i]; }
        else if (!strcmp(a, "--replay"))     { need(++i, argc, a); o_replay = argv[i]; }
        else if (!strcmp(a, "--timeout"))    { need(++i, argc, a); o_timeout_sec = atoi(argv[i]); }
        else if (!strcmp(a, "--append"))     o_append = 1;
        else if (!strcmp(a, "--timestamp"))  o_timestamp = 1;
        else if (!strcmp(a, "--echo"))       o_echo = 1;
        else if (!strcmp(a, "--no-reconnect")) o_reconnect = 0;
        else if (!strcmp(a, "--quiet"))      o_quiet = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--version"))    { printf("%s %s\n", PROG_NAME, PROG_VERSION); return 0; }
        else { fprintf(stderr, PROG_NAME ": unknown option %s\n", a); usage(); return 2; }
    }

    if (o_replay) {
        if (o_port)   { fprintf(stderr, PROG_NAME ": --replay does not take --port\n"); return 2; }
        if (o_record) { fprintf(stderr, PROG_NAME ": --replay and --record are mutually exclusive\n"); return 2; }
        if (!o_out)   { usage(); return 2; }
    } else if (!o_port || !o_out) {
        usage(); return 2;
    }
    if (o_flush_ms < 50) o_flush_ms = 50;
    if (o_timeout_sec > 0) {
        g_deadline = GetTickCount() + (DWORD)o_timeout_sec * 1000;
        g_have_deadline = 1;
    }

    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    SetUnhandledExceptionFilter(crash_handler);

    /* A kill file left behind by a previous run would stop us instantly.
     * Clear it and say so, rather than exiting mysteriously. */
    if (o_kill && file_exists(o_kill)) {
        logmsg("stale kill file %s found at startup, removing", o_kill);
        DeleteFileA(o_kill);
    }

    if (o_replay) return run_replay();

    if (!o_append) {
        HANDLE t = CreateFileA(o_out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
    }

    g_port = open_port();
    if (g_port == INVALID_HANDLE_VALUE) {
        fprintf(stderr, PROG_NAME ": cannot open %s (error %lu)\n", o_port, GetLastError());
        return 3;
    }
    logmsg("%s open at %d %d%c%s, logging to %s%s", o_port, o_baud, o_data,
           o_parity, o_stop, o_out, o_timestamp ? " (timestamped)" : "");

    if (o_record && !record_open()) {
        logmsg("cannot open %s.raw/.timing for --record, continuing without it", o_record);
        o_record = NULL;
    }

    last_flush = GetTickCount();

    while (!g_stop) {
        if (o_kill && file_exists(o_kill)) {
            logmsg("kill file %s present, stopping", o_kill);
            break;
        }
        if (timeout_hit()) {
            logmsg("timeout of %ds reached with no kill file or Ctrl-C, stopping", o_timeout_sec);
            rc = 6;
            break;
        }

        got = 0;
        if (g_port != INVALID_HANDLE_VALUE &&
            ReadFile(g_port, chunk, sizeof(chunk), &got, NULL)) {
            if (got) {
                if (o_record) record_chunk(chunk, got);
                if (o_timestamp) now_stamp(stamp, sizeof(stamp));
                if (!ob_append_chunk(&ob, chunk, got, o_timestamp ? stamp : NULL)) {
                    logmsg("out of memory buffering %u bytes", (unsigned)got);
                    rc = 4; break;
                }
                total += got;
                if (o_echo) { fwrite(chunk, 1, got, stdout); fflush(stdout); }
            }
        } else if (g_port != INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            logmsg("read failed on %s (%lu)", o_port, e);
            CloseHandle(g_port);
            g_port = INVALID_HANDLE_VALUE;
            if (!o_reconnect) { rc = 5; break; }
        }

        /* USB serial adapters get unplugged. Keep the log file and the kill
         * file semantics alive across a re-plug instead of dying. */
        if (g_port == INVALID_HANDLE_VALUE && o_reconnect) {
            flush_to_file(&ob);
            Sleep(1000);
            g_port = open_port();
            if (g_port != INVALID_HANDLE_VALUE) logmsg("%s reopened", o_port);
            continue;
        }

        now = GetTickCount();
        if ((DWORD)(now - last_flush) >= (DWORD)o_flush_ms) {   /* wrap-safe */
            flush_to_file(&ob);
            last_flush = now;
        }
    }

    flush_to_file(&ob);
    if (g_port != INVALID_HANDLE_VALUE) CloseHandle(g_port);
    g_port = INVALID_HANDLE_VALUE;
    if (o_record) record_close();
    ob_free(&ob);

    if (o_kill && file_exists(o_kill)) DeleteFileA(o_kill);
    logmsg("stopped after %llu bytes", total);
    return rc;
}

#endif /* _WIN32 */