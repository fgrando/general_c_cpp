//mingw32-gcc -O2 -Wall powercontrol2udp.c -o powercontrol2udp.exe -lws2_32 powercontrol2udp.exe
/*
 * powercontrol2udp.c -- UDP bridge between the web UI and a Rigol PSU.
 *
 * A Winsock UDP server that speaks the same protocol as the mock:
 *     "status"            -> JSON status of all channels
 *     "output <ch> on"    -> set output, reply status
 *     "output <ch> off"   -> set output, reply status
 * and drives the instrument over VISA (visa32.dll, loaded at runtime, so no
 * NI-VISA import library is needed to build -- only the VISA runtime to run).
 *
 * Build (32-bit "mingw32"):
 *     i686-w64-mingw32-gcc  -O2 -Wall powercontrol2udp.c -o powercontrol2udp.exe -lws2_32
 * Build (64-bit):
 *     x86_64-w64-mingw32-gcc -O2 -Wall powercontrol2udp.c -o powercontrol2udp.exe -lws2_32
 *
 * Run it, then start psu_web.py pointing at BRIDGE_HOST/PORT (defaults match).
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------- configuration ------------------------------- */
#define BIND_HOST     "127.0.0.1"   /* use "0.0.0.0" to reach it from other hosts */
#define BIND_PORT     5005
#define MAX_CHANNELS  3             /* probe CH1..CH<MAX> at connect */
static const char *RESOURCE = NULL; /* NULL = auto-detect; or "USB0::0x1AB1::...::INSTR" */
static const char *RIGOL_EXPR   = "USB?*::0x1AB1::?*::INSTR"; /* 0x1AB1 = Rigol VID */
static const char *ANY_USB_EXPR = "USB?*INSTR";
/* ----------------------------------------------------------------------- */

/* --- minimal VISA ABI (avoids needing visa.h); __stdcall on Win32 ------ */
#define VISA_CALL __stdcall
typedef uint32_t  ViSession, ViUInt32, ViAttr, ViAttrState, ViObject, ViFindList;
typedef int32_t   ViStatus;
typedef char      ViChar;
#define VI_NULL            0
#define VI_SUCCESS         0
#define VI_ATTR_TMO_VALUE  0x3FFF001AUL
#define VI_FIND_BUFLEN     256

typedef ViStatus (VISA_CALL *fp_OpenDefaultRM)(ViSession *);
typedef ViStatus (VISA_CALL *fp_FindRsrc)(ViSession, ViChar *, ViFindList *, ViUInt32 *, ViChar *);
typedef ViStatus (VISA_CALL *fp_FindNext)(ViFindList, ViChar *);
typedef ViStatus (VISA_CALL *fp_Open)(ViSession, ViChar *, ViUInt32, ViUInt32, ViSession *);
typedef ViStatus (VISA_CALL *fp_Close)(ViObject);
typedef ViStatus (VISA_CALL *fp_SetAttribute)(ViObject, ViAttr, ViAttrState);
typedef ViStatus (VISA_CALL *fp_Write)(ViSession, const unsigned char *, ViUInt32, ViUInt32 *);
typedef ViStatus (VISA_CALL *fp_Read)(ViSession, unsigned char *, ViUInt32, ViUInt32 *);

static fp_OpenDefaultRM viOpenDefaultRM;
static fp_FindRsrc      viFindRsrc;
static fp_FindNext      viFindNext;
static fp_Open          viOpen;
static fp_Close         viClose;
static fp_SetAttribute  viSetAttribute;
static fp_Write         viWrite;
static fp_Read          viRead;

/* --- instrument state -------------------------------------------------- */
static ViSession g_rm = 0, g_dev = 0;
static int       g_connected = 0;
static int       g_chan[MAX_CHANNELS];
static int       g_nchan = 0;
static char      g_idn[256] = {0};

/* ----------------------------------------------------------------------- */
static int load_visa(void)
{
    HMODULE h = LoadLibraryA("visa32.dll");
    if (!h) { fprintf(stderr, "Cannot load visa32.dll (VISA runtime installed?)\n"); return 0; }
#define BIND(n) do{ FARPROC f_ = GetProcAddress(h,"vi" #n); \
        if(!f_){ fprintf(stderr,"Missing export vi" #n "\n"); return 0; } \
        memcpy(&vi##n, &f_, sizeof f_); }while(0)
    BIND(OpenDefaultRM); BIND(FindRsrc); BIND(FindNext); BIND(Open);
    BIND(Close); BIND(SetAttribute); BIND(Write); BIND(Read);
#undef BIND
    return 1;
}

static void scpi_write(const char *cmd)
{
    char buf[128]; ViUInt32 n = 0;
    int len = snprintf(buf, sizeof buf, "%s\n", cmd);
    if (g_dev) viWrite(g_dev, (const unsigned char *)buf, (ViUInt32)len, &n);
}

/* Query; return 1 and fill out (trimmed) on success, 0 on failure. */
static int scpi_query(const char *cmd, char *out, size_t outsz)
{
    ViUInt32 n = 0;
    if (!g_dev) return 0;
    scpi_write(cmd);
    if (viRead(g_dev, (unsigned char *)out, (ViUInt32)(outsz - 1), &n) < VI_SUCCESS) return 0;
    if (n >= outsz) n = (ViUInt32)(outsz - 1);
    out[n] = '\0';
    while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
    return 1;
}

static void visa_close(void)
{
    if (g_dev) viClose(g_dev);
    if (g_rm)  viClose(g_rm);
    g_dev = g_rm = 0;
    g_connected = 0;
}

static int find_first(const char *expr, char *desc)
{
    ViFindList fl = 0; ViUInt32 cnt = 0;
    if (viFindRsrc(g_rm, (ViChar *)expr, &fl, &cnt, desc) >= VI_SUCCESS && cnt) {
        viClose(fl); return 1;
    }
    return 0;
}

static int visa_connect(void)
{
    char desc[VI_FIND_BUFLEN] = {0};
    visa_close();
    if (viOpenDefaultRM(&g_rm) < VI_SUCCESS) return 0;

    if (RESOURCE) { strncpy(desc, RESOURCE, sizeof desc - 1); }
    else if (!find_first(RIGOL_EXPR, desc) && !find_first(ANY_USB_EXPR, desc)) {
        return 0;
    }
    if (viOpen(g_rm, (ViChar *)desc, VI_NULL, VI_NULL, &g_dev) < VI_SUCCESS) { g_dev = 0; return 0; }
    viSetAttribute(g_dev, VI_ATTR_TMO_VALUE, 3000);

    if (!scpi_query("*IDN?", g_idn, sizeof g_idn)) { visa_close(); return 0; }

    /* Probe which channels answer. */
    g_nchan = 0;
    for (int ch = 1; ch <= MAX_CHANNELS; ++ch) {
        char q[32], r[32];
        snprintf(q, sizeof q, ":OUTP? CH%d", ch);
        if (scpi_query(q, r, sizeof r) && r[0]) g_chan[g_nchan++] = ch;
        else break;
    }
    if (g_nchan == 0) g_chan[g_nchan++] = 1;   /* assume single channel */
    g_connected = 1;
    return 1;
}

/* --- JSON assembly ----------------------------------------------------- */
typedef struct { char *p; size_t cap; size_t len; } Buf;

static void bappend(Buf *b, const char *fmt, ...)
{
    if (b->len + 1 >= b->cap) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t room = b->cap - b->len - 1;
        b->len += ((size_t)n < room) ? (size_t)n : room;
    }
}

/* Append a JSON string value with minimal escaping. */
static void bappend_jstr(Buf *b, const char *s)
{
    bappend(b, "\"");
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') bappend(b, "\\%c", *s);
        else if ((unsigned char)*s < 0x20) bappend(b, " ");
        else bappend(b, "%c", *s);
    }
    bappend(b, "\"");
}

/* Query a numeric SCPI value; append it, or "null" on failure. */
static void bappend_num(Buf *b, const char *cmd, int dp)
{
    char raw[64];
    if (scpi_query(cmd, raw, sizeof raw) && raw[0]) bappend(b, "%.*f", dp, atof(raw));
    else bappend(b, "null");
}

static void build_status(Buf *b)
{
    if (!g_connected && !visa_connect()) {
        bappend(b, "{\"connected\":false,\"error\":\"No Rigol PSU found on USB.\",\"channels\":[]}");
        return;
    }
    bappend(b, "{\"connected\":true,\"idn\":");
    bappend_jstr(b, g_idn);
    bappend(b, ",\"channels\":[");
    for (int i = 0; i < g_nchan; ++i) {
        int ch = g_chan[i];
        char q[40], r[40];
        snprintf(q, sizeof q, ":OUTP? CH%d", ch);
        if (!scpi_query(q, r, sizeof r)) {          /* link dropped */
            visa_close();
            b->len = 0; b->p[0] = '\0';
            bappend(b, "{\"connected\":false,\"error\":\"Lost connection to instrument.\",\"channels\":[]}");
            return;
        }
        int on = (r[0]=='O'&&(r[1]=='N'||r[1]=='n')) || r[0]=='1';
        if (i) bappend(b, ",");
        bappend(b, "{\"ch\":%d,\"output\":%s,\"vmeas\":", ch, on ? "true" : "false");
        snprintf(q, sizeof q, ":MEAS:VOLT? CH%d", ch); bappend_num(b, q, 3);
        bappend(b, ",\"imeas\":");
        snprintf(q, sizeof q, ":MEAS:CURR? CH%d", ch); bappend_num(b, q, 3);
        bappend(b, ",\"pmeas\":");
        snprintf(q, sizeof q, ":MEAS:POWE? CH%d", ch); bappend_num(b, q, 2);
        bappend(b, ",\"vset\":");
        snprintf(q, sizeof q, ":SOUR%d:VOLT?", ch);    bappend_num(b, q, 3);
        bappend(b, ",\"iset\":");
        snprintf(q, sizeof q, ":SOUR%d:CURR?", ch);    bappend_num(b, q, 3);
        bappend(b, "}");
    }
    bappend(b, "]}");
}

/* --- command dispatch -------------------------------------------------- */
static void handle(const char *req, char *reply, size_t replysz)
{
    Buf b = { reply, replysz, 0 };
    char verb[16] = {0}, st[8] = {0};
    int ch = 0;

    if (sscanf(req, "%15s", verb) != 1) verb[0] = '\0';

    if (strcmp(verb, "output") == 0 && sscanf(req, "%*s %d %7s", &ch, st) == 2) {
        if (g_connected || visa_connect()) {
            /* DP800/DP900/DP2000 syntax. Adjust here if your model differs. */
            char cmd[40];
            snprintf(cmd, sizeof cmd, ":OUTP CH%d,%s", ch,
                     (strcmp(st, "on") == 0) ? "ON" : "OFF");
            scpi_write(cmd);
        }
        build_status(&b);
    } else {                          /* "status" or anything else */
        build_status(&b);
    }
}

/* ----------------------------------------------------------------------- */
int main(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
    if (!load_visa()) return 1;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { fprintf(stderr, "socket() failed\n"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BIND_PORT);
    addr.sin_addr.s_addr = inet_addr(BIND_HOST);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed on %s:%d\n", BIND_HOST, BIND_PORT);
        return 1;
    }

    if (visa_connect())
        printf("powercontrol2udp on %s:%d  ->  %s\n", BIND_HOST, BIND_PORT, g_idn);
    else
        printf("powercontrol2udp on %s:%d  (no PSU yet; will retry on request)\n", BIND_HOST, BIND_PORT);
    fflush(stdout);

    char req[512], reply[4096];
    struct sockaddr_in from;
    int fromlen;
    for (;;) {
        fromlen = sizeof from;
        int n = recvfrom(s, req, sizeof req - 1, 0, (struct sockaddr *)&from, &fromlen);
        if (n == SOCKET_ERROR) continue;
        req[n] = '\0';
        /* trim trailing whitespace/newlines from the datagram */
        while (n && (req[n-1] == '\n' || req[n-1] == '\r' || req[n-1] == ' ')) req[--n] = '\0';

        handle(req, reply, sizeof reply);
        sendto(s, reply, (int)strlen(reply), 0, (struct sockaddr *)&from, fromlen);
    }
    /* not reached */
}