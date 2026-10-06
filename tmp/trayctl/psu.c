/* psu.c - power supply control: 3 independent output channels, each with a
 * label and the commands that drive it, all read from the ini:
 *
 *   chN_label = name shown in the menu and notifications
 *   chN_on    = command that switches the channel on,  e.g. psu.exe SW1_ON
 *   chN_off   = command that switches the channel off, e.g. psu.exe SW1_OFF
 *   chN_read  = command that prints the channel voltage on stdout; the first
 *               number in its output is taken as the reading, in volts
 *
 * Commands run through "cmd.exe /c" with no console window, so .bat files
 * and shell built-ins work too. Exit code 0 means success; anything else
 * (or a command that runs past CMD_TIMEOUT_MS) is reported as a failure and
 * leaves the channel's state unchanged.
 *
 * Menu: a grayed summary line shows the last voltage read from every
 * channel. Channel 1 sits at the top level as a shortcut, and a "PSU"
 * submenu holds all three, so two rarely-touched channels don't crowd the
 * main menu. Both copies of channel 1 use the same command ID.
 *
 * Threading: running a command blocks, and every plugin shares one UI
 * thread, so all commands run on a worker thread. command() only queues a
 * request; the worker runs it, then re-reads every voltage (also every
 * READ_INTERVAL_MS). Results are handed back under a lock, and
 * host->set_alert()/host->notify() are only ever called from tick() on the
 * UI thread, never from the worker.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>   /* strtod */
#include <ctype.h>

/* Module identity: name in the Module struct, ini section, alert source. */
#define MOD "psu"

#define NUM_CHANNELS     3
#define CH_NAME_LEN      32
#define CMD_LEN          512
#define READ_INTERVAL_MS 5000
#define CMD_TIMEOUT_MS   10000

typedef struct {
    /* Set once in init(), read-only afterwards - no lock needed. */
    char name[CH_NAME_LEN];
    char on_cmd[CMD_LEN];
    char off_cmd[CMD_LEN];
    char read_cmd[CMD_LEN];

    /* Shared with the worker - guarded by `lock`. */
    int    on;          /* last state a command succeeded in setting */
    int    request;     /* -1 = none, else the state to switch to */
    int    busy;        /* an on/off command is running */
    double volts;
    int    volts_ok;    /* 0 until a read succeeds, or after one fails */
    int    report;      /* 0 = nothing, 1 = switched, 2 = failed; for tick() */
    char   err[64];
} Channel;

static const HostAPI *host;
static int enabled;
static Channel channels[NUM_CHANNELS];

static CRITICAL_SECTION lock;
static HANDLE wake;              /* auto-reset: a request was queued */
static volatile LONG stopping;

/* Append what fits of buf[0..got) to out; the rest is dropped. */
static void append(char *out, int out_size, int *len, const char *buf, DWORD got)
{
    int n = out_size - 1 - *len;
    if (n <= 0) return;
    if ((int)got < n) n = (int)got;
    memcpy(out + *len, buf, (size_t)n);
    *len += n;
    out[*len] = '\0';
}

/* Run `cmd` hidden via cmd.exe, capture up to out_size-1 bytes of its
 * stdout+stderr into `out`. Returns the exit code, or -1 with `err` filled
 * if it couldn't be started or timed out. Worker thread only. */
static int run_cmd(const char *cmd, char *out, int out_size,
                   char *err, int err_size)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd, wr;
    char line[CMD_LEN + 16], buf[256];
    DWORD got, avail, code = (DWORD)-1, start;
    int len = 0, done = 0;

    if (out_size > 0) out[0] = '\0';
    if (cmd[0] == '\0') { lstrcpynA(err, "no command configured", err_size); return -1; }
    if (!CreatePipe(&rd, &wr, &sa, 0)) { lstrcpynA(err, "CreatePipe failed", err_size); return -1; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    snprintf(line, sizeof(line), "cmd.exe /c %s", cmd);

    if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(err, (size_t)err_size, "could not start (error %lu)", GetLastError());
        CloseHandle(rd); CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);   /* so the pipe reports EOF once the child is gone */

    /* Poll instead of a blocking ReadFile, so a hung command can still
     * time out (and a process it leaves behind holding the pipe can't hang
     * us). Output past `out` is read and dropped to keep the child from
     * blocking on a full pipe. */
    start = GetTickCount();
    while (!done) {
        done = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;
        while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail > 0 &&
               ReadFile(rd, buf, sizeof(buf), &got, NULL) && got > 0)
            append(out, out_size, &len, buf, got);
        if (!done && (GetTickCount() - start > CMD_TIMEOUT_MS || stopping)) {
            TerminateProcess(pi.hProcess, 1);
            lstrcpynA(err, "timed out", err_size);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
            return -1;
        }
    }

    GetExitCodeProcess(pi.hProcess, &code);
    if (code != 0) snprintf(err, (size_t)err_size, "exit code %lu", code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return (int)code;
}

/* First number in `s` that isn't glued to a word, so the channel number in
 * e.g. "CH1 VOLT 5.012\r\n" is skipped and 5.012 is returned. */
static int parse_volts(const char *s, double *v)
{
    const char *p;
    char *end;
    for (p = s; *p; p++) {
        if (p > s && (isalnum((unsigned char)p[-1]) || p[-1] == '.')) continue;
        if (isdigit((unsigned char)*p) ||
            ((*p == '-' || *p == '+' || *p == '.') && isdigit((unsigned char)p[1]))) {
            *v = strtod(p, &end);
            if (end != p) return 1;
        }
    }
    return 0;
}

static void worker_switch(int idx, int on)
{
    char out[64], err[64] = "";
    int ok = run_cmd(on ? channels[idx].on_cmd : channels[idx].off_cmd,
                     out, sizeof(out), err, sizeof(err)) == 0;

    EnterCriticalSection(&lock);
    if (ok) channels[idx].on = on;
    channels[idx].busy = 0;
    channels[idx].report = ok ? 1 : 2;
    lstrcpynA(channels[idx].err, err, sizeof(channels[idx].err));
    LeaveCriticalSection(&lock);
}

static void worker_read(int idx)
{
    char out[256], err[64];
    double v = 0.0;
    int ok;

    if (channels[idx].read_cmd[0] == '\0') return;
    ok = run_cmd(channels[idx].read_cmd, out, sizeof(out), err, sizeof(err)) == 0 &&
         parse_volts(out, &v);

    EnterCriticalSection(&lock);
    channels[idx].volts = v;
    channels[idx].volts_ok = ok;
    LeaveCriticalSection(&lock);
}

/* Worker thread: nothing here may touch host-> or the UI. */
static DWORD WINAPI worker_proc(LPVOID unused)
{
    int i, req;
    (void)unused;

    while (!stopping) {
        for (i = 0; i < NUM_CHANNELS && !stopping; i++) {
            EnterCriticalSection(&lock);
            req = channels[i].request;
            channels[i].request = -1;
            if (req >= 0) channels[i].busy = 1;
            LeaveCriticalSection(&lock);
            if (req >= 0) worker_switch(i, req);
        }
        for (i = 0; i < NUM_CHANNELS && !stopping; i++) worker_read(i);
        WaitForSingleObject(wake, READ_INTERVAL_MS);
    }
    return 0;
}

static void psu_init(const HostAPI *h)
{
    char key[16], def[CH_NAME_LEN];
    HANDLE t;
    int i;

    host = h;
    enabled = host->ini_int(MOD, "enabled", 1);
    for (i = 0; i < NUM_CHANNELS; i++) {
        Channel *c = &channels[i];

        /* chN_label, falling back to the older chN_name key. */
        snprintf(key, sizeof(key), "ch%d_name", i + 1);
        snprintf(def, sizeof(def), "Channel %d", i + 1);
        host->ini_str(MOD, key, def, def, sizeof(def));
        snprintf(key, sizeof(key), "ch%d_label", i + 1);
        host->ini_str(MOD, key, def, c->name, sizeof(c->name));

        snprintf(key, sizeof(key), "ch%d_on", i + 1);
        host->ini_str(MOD, key, "", c->on_cmd, sizeof(c->on_cmd));
        snprintf(key, sizeof(key), "ch%d_off", i + 1);
        host->ini_str(MOD, key, "", c->off_cmd, sizeof(c->off_cmd));
        snprintf(key, sizeof(key), "ch%d_read", i + 1);
        host->ini_str(MOD, key, "", c->read_cmd, sizeof(c->read_cmd));

        c->on = 0;   /* actual state is unknown at startup; assume off */
        c->request = -1;
    }
    if (!enabled) return;

    InitializeCriticalSection(&lock);
    wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    t = CreateThread(NULL, 0, worker_proc, NULL, 0, NULL);
    if (t != NULL) CloseHandle(t);   /* the host never unloads plugin DLLs */
}

/* Adopt worker results. UI thread only - host->set_alert and host->notify
 * must not be called from the worker (see main.c). */
static void psu_tick(void)
{
    char msg[128];
    int i, any = 0;

    if (!enabled) return;
    EnterCriticalSection(&lock);
    for (i = 0; i < NUM_CHANNELS; i++) {
        Channel *c = &channels[i];
        any |= c->on;
        if (c->report == 1) {
            snprintf(msg, sizeof(msg), "%.31s: %s", c->name, c->on ? "ON" : "OFF");
            host->notify("PSU", msg, NIIF_INFO);
        } else if (c->report == 2) {
            snprintf(msg, sizeof(msg), "%.31s: switching failed - %.63s", c->name, c->err);
            host->notify("PSU", msg, NIIF_ERROR);
        }
        c->report = 0;
    }
    LeaveCriticalSection(&lock);
    host->set_alert(MOD, any);
}

/* "<prefix><label>: ON  - click to turn OFF". Caller holds `lock`. */
static void fmt_channel(char *buf, int size, const char *prefix, int idx)
{
    const Channel *c = &channels[idx];

    if (c->busy || c->request >= 0)
        snprintf(buf, (size_t)size, "%s%s: switching...", prefix, c->name);
    else
        snprintf(buf, (size_t)size, "%s%s: %s  - click to turn %s",
                 prefix, c->name, c->on ? "ON" : "OFF", c->on ? "OFF" : "ON");
}

static void psu_menu(HMENU menu, UINT base)
{
    HMENU sub;
    char text[256];
    int i, len;

    if (!enabled) return;
    EnterCriticalSection(&lock);

    /* "PSU  5V Rail 5.01 V | 12V Rail 0.00 V | Aux -- V" - display only. */
    len = snprintf(text, sizeof(text), "PSU ");
    for (i = 0; i < NUM_CHANNELS && len > 0 && len < (int)sizeof(text); i++) {
        if (channels[i].volts_ok)
            len += snprintf(text + len, sizeof(text) - (size_t)len, "%s %s %.2f V",
                            i ? " |" : "", channels[i].name, channels[i].volts);
        else
            len += snprintf(text + len, sizeof(text) - (size_t)len, "%s %s -- V",
                            i ? " |" : "", channels[i].name);
    }
    AppendMenuA(menu, MF_STRING | MF_GRAYED, base, text);

    /* Channel 1 is promoted to the top level as a shortcut - it's the one
     * reached for most often. */
    fmt_channel(text, sizeof(text), "PSU ", 0);
    AppendMenuA(menu, MF_STRING | (channels[0].on ? MF_CHECKED : 0),
                base + 1u, text);

    /* Everything, including channel 1 again, lives under a "PSU" submenu so
     * the main menu stays short. Both entries carry the same command ID, so
     * either one toggles the same channel. */
    sub = CreatePopupMenu();
    for (i = 0; i < NUM_CHANNELS; i++) {
        if (i > 0) AppendMenuA(sub, MF_SEPARATOR, 0, NULL);
        fmt_channel(text, sizeof(text), "", i);   /* submenu is already titled */
        AppendMenuA(sub, MF_STRING | (channels[i].on ? MF_CHECKED : 0),
                    base + (UINT)i + 1u, text);
    }
    LeaveCriticalSection(&lock);
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)sub, "PSU");
}

/* Queue the toggle; the worker runs the command. Clicking again before it
 * runs flips the request back. */
static void psu_command(UINT off)
{
    int idx = (int)off - 1;
    Channel *c;

    if (!enabled || idx < 0 || idx >= NUM_CHANNELS) return;
    c = &channels[idx];
    EnterCriticalSection(&lock);
    c->request = c->request >= 0 ? !c->request : !c->on;
    LeaveCriticalSection(&lock);
    SetEvent(wake);
}

/* The host's tooltip line is short, so summarize rather than listing three
 * labels that would just get truncated. */
static void psu_tip(char *buf, int size)
{
    int i, on = 0;

    if (!enabled) return;
    EnterCriticalSection(&lock);
    for (i = 0; i < NUM_CHANNELS; i++) on += channels[i].on;
    LeaveCriticalSection(&lock);
    snprintf(buf, (size_t)size, "PSU: %d of %d channels on", on, NUM_CHANNELS);
}

/* Stop the worker and kill any command it is waiting on. The DLL is never
 * unloaded, so a thread still finishing up can't fault. */
static void psu_shutdown(void)
{
    if (!enabled) return;
    InterlockedExchange(&stopping, 1);
    SetEvent(wake);
}

static const Module psu_module = {
    MOD, "1.0.0",
    psu_init, psu_menu, psu_command, psu_tick, psu_tip, psu_shutdown
};

TRAYCTL_PLUGIN(psu_module)
