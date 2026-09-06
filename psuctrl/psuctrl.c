/*
 * psuctrl.c -- system-tray controller for a VISA power supply.
 *
 * This is a refactor of the old UDP bridge: the network layer is gone.
 * Everything is now driven from a Windows tray icon:
 *
 *   - the tray menu shows the status of every channel, refreshed every
 *     refresh_ms milliseconds (configurable);
 *   - user-defined VISA command sequences (INI [commands] section) show up
 *     as clickable menu entries;
 *   - the tray icon is drawn live: one horizontal stripe per channel,
 *     red when that output is on, gray when off;
 *   - mock mode (mock=1 in the INI, or --mock on the command line) runs an
 *     in-process PSU simulator so the UI can be exercised with no hardware
 *     and no VISA runtime installed;
 *   - timeout_ms sets the VISA I/O timeout;
 *   - kill_file: drop that file next to the exe and the app deletes it and
 *     exits (handy for scripted teardown).
 *
 * Headless / scriptable mode (also see the separate psuctrl_status.exe):
 *     psuctrl --list                    list command labels
 *     psuctrl --status                  print per-channel status lines
 *     psuctrl --run "LABEL"             run one configured command sequence
 *   ...each accepts --mock and --config PATH, prints to the caller's stdout,
 *   and returns 0 ok / 2 bad usage / 3 no VISA runtime / 4 no instrument /
 *   5 instrument did not respond. (Don't run these against real hardware
 *   while the tray instance is connected -- VISA allows one session only.)
 *
 * The instrument core (VISA loader, mock PSU, SCPI dispatch, INI parser)
 * lives in psuctrl_common.c.
 *
 * Build (64-bit):
 *     x86_64-w64-mingw32-gcc -O2 -Wall psuctrl.c psuctrl_common.c -o psuctrl.exe -mwindows -lshell32 -lgdi32
 * Build (32-bit "mingw32"):
 *     i686-w64-mingw32-gcc   -O2 -Wall psuctrl.c psuctrl_common.c -o psuctrl.exe -mwindows -lshell32 -lgdi32
 */

#include "psuctrl_common.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wstringop-truncation"

/* ------------------------- message / menu ids ---------------------- */
#define WM_TRAY        (WM_APP + 1)
#define WM_REFRESH_TIP (WM_APP + 2)
#define WM_BALLOON     (WM_APP + 3)

#define ID_EXIT        0x10
#define ID_RECONNECT   0x11
#define ID_RELOAD      0x12
#define ID_OPENCFG     0x13
#define ID_STATUS      0x14
#define ID_CMD_BASE    0x100

#define PEND_STATUS    (-2)   /* g_pending sentinel: run the built-in status */

/* --------------------------- shared state -------------------------- */
typedef struct {
    int  connected;
    char header[192];
    int  nlines;
    char lines[MAX_CHANNELS][192];
    char tip[256];
    int  non;                 /* stripes to draw on the tray icon      */
    int  on[MAX_CHANNELS];    /* per-channel output state (1 = on/red)  */
} Snapshot;

static CRITICAL_SECTION g_lock;
static Snapshot g_snap;
static struct { char title[64]; char text[256]; } g_balloon;

static volatile int g_pending    = -1;  /* command index queued by the UI    */
static volatile int g_reconnect  = 0;
static volatile int g_reload     = 0;
static volatile int g_quit       = 0;

static HWND     g_hwnd;
static HANDLE   g_worker;
static HANDLE   g_wake;
static NOTIFYICONDATAA g_nid;
static HICON    g_cur_icon;   /* dynamically drawn per-channel stripe icon */

/* forward decls */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static void show_menu(HWND);

/* =================================================================== */
/*  polling                                                             */
/* =================================================================== */
static void poll_once(Snapshot *s)
{
    memset(s, 0, sizeof *s);
    int n = g_cfg.channels;
    if (n < 1) n = 1;
    if (n > MAX_CHANNELS) n = MAX_CHANNELS;
    s->non = n;   /* icon always shows n stripes; all gray until proven on */

    if (g_cfg.mock) {
        s->connected = 1;
        snprintf(s->header, sizeof s->header, "MOCK MODE  -  simulated PSU");
    } else if (!g_visa_loaded) {
        s->connected = 0;
        snprintf(s->header, sizeof s->header, "visa32.dll not found - install a VISA runtime");
    } else if (!g_connected && !visa_connect()) {
        s->connected = 0;
        snprintf(s->header, sizeof s->header, "Not connected - no VISA instrument found");
    } else {
        s->connected = 1;
        snprintf(s->header, sizeof s->header, "%s", g_idn);
    }

    if (s->connected) {
        for (int ch = 1; ch <= n; ++ch) {
            char line[192];
            if (!expand_line(g_cfg.status_tmpl, ch, line, sizeof line)) {
                if (!g_cfg.mock) visa_close();
                s->connected = 0;
                s->nlines = 0;
                snprintf(s->header, sizeof s->header, "Lost connection to instrument");
                break;
            }
            snprintf(s->lines[s->nlines++], sizeof s->lines[0], "%s", line);

            /* output state for the tray-icon stripe */
            char chs[16], q[128], r[64];
            snprintf(chs, sizeof chs, "%d", ch);
            replace_all(g_cfg.output_query, "{ch}", chs, q, sizeof q);
            if (q[0] && scpi_query(q, r, sizeof r)) {
                str_trim(r);
                s->on[ch - 1] = (toupper((unsigned char)r[0]) == 'O' &&
                                 toupper((unsigned char)r[1]) == 'N') || r[0] == '1';
            }
        }
    }

    /* tooltip: channel lines, or the header when there are none */
    size_t t = 0;
    for (int i = 0; i < s->nlines && t + 1 < sizeof s->tip; ++i) {
        int w = snprintf(s->tip + t, sizeof s->tip - t, "%s%s", i ? "\n" : "", s->lines[i]);
        if (w > 0) t += ((size_t)w < sizeof s->tip - t) ? (size_t)w : sizeof s->tip - t - 1;
    }
    if (s->nlines == 0)
        snprintf(s->tip, sizeof s->tip, "%s", s->header);
}

/* =================================================================== */
/*  command sequences                                                   */
/* =================================================================== */
static void notify(const char *title, const char *text)
{
    EnterCriticalSection(&g_lock);
    snprintf(g_balloon.title, sizeof g_balloon.title, "%s", title);
    snprintf(g_balloon.text,  sizeof g_balloon.text,  "%s", text);
    LeaveCriticalSection(&g_lock);
    PostMessage(g_hwnd, WM_BALLOON, 0, 0);
}

static void exec_sequence(int idx)
{
    if (idx < 0 || idx >= g_ncmds) return;
    char info[256];
    run_sequence(g_cmds[idx].seq, info, sizeof info);
    notify(g_cmds[idx].label, info[0] ? info : "sent");
}

/* =================================================================== */
/*  worker thread: owns all instrument I/O                              */
/* =================================================================== */
static DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    for (;;) {
        if (g_quit) break;

        if (g_reload) {
            g_reload = 0;
            config_load(g_cfgpath);
            if (g_force_mock) g_cfg.mock = 1;
        }

        /* kill file */
        if (g_cfg.kill_file[0]) {
            char kp[MAX_PATH];
            resolve_path(g_cfg.kill_file, kp, sizeof kp);
            if (GetFileAttributesA(kp) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(kp);
                g_quit = 1;
                PostMessage(g_hwnd, WM_CLOSE, 0, 0);
                break;
            }
        }

        if (g_reconnect) {
            g_reconnect = 0;
            if (!g_cfg.mock) visa_close();
        }

        int pend;
        EnterCriticalSection(&g_lock);
        pend = g_pending; g_pending = -1;
        LeaveCriticalSection(&g_lock);
        if (pend == PEND_STATUS) {
            char info[512];
            builtin_status(info, sizeof info);
            notify(BUILTIN_STATUS, info[0] ? info : "(no data)");
        } else if (pend >= 0) {
            exec_sequence(pend);
        }

        Snapshot local;
        poll_once(&local);
        EnterCriticalSection(&g_lock);
        g_snap = local;
        LeaveCriticalSection(&g_lock);
        PostMessage(g_hwnd, WM_REFRESH_TIP, 0, 0);

        int wait = g_cfg.refresh_ms;
        if (wait < 100) wait = 100;
        WaitForSingleObject(g_wake, wait);
    }
    return 0;
}

/* =================================================================== */
/*  tray UI                                                             */
/* =================================================================== */

/*
 * Draw the tray icon: n horizontal stripes, one per channel, red when the
 * output is on and gray when it is off (top stripe = channel 1).
 */
static HICON build_stripe_icon(const int *on, int n)
{
    const int W = 16, H = 16;
    if (n < 1) n = 1;
    if (n > MAX_CHANNELS) n = MAX_CHANNELS;

    HDC     screen = GetDC(NULL);
    HDC     dc     = CreateCompatibleDC(screen);
    HBITMAP color  = CreateCompatibleBitmap(screen, W, H);
    HBITMAP mask   = CreateBitmap(W, H, 1, 1, NULL);   /* all 0 => opaque */
    ReleaseDC(NULL, screen);

    HBITMAP oldbmp = (HBITMAP)SelectObject(dc, color);
    RECT    full   = { 0, 0, W, H };

    HBRUSH frame = CreateSolidBrush(RGB(40, 40, 40));
    HBRUSH red   = CreateSolidBrush(RGB(210, 45, 45));
    HBRUSH gray  = CreateSolidBrush(RGB(105, 105, 105));

    FillRect(dc, &full, frame);                        /* border / gaps */
    for (int i = 0; i < n; ++i) {
        RECT band = { 1,
                      1 + i * (H - 2) / n,
                      W - 1,
                      ((i + 1) == n) ? H - 1 : (1 + (i + 1) * (H - 2) / n - 1) };
        FillRect(dc, &band, on[i] ? red : gray);
    }

    DeleteObject(frame);
    DeleteObject(red);
    DeleteObject(gray);

    SelectObject(dc, mask);
    RECT mfull = { 0, 0, W, H };
    FillRect(dc, &mfull, (HBRUSH)GetStockObject(BLACK_BRUSH));

    SelectObject(dc, oldbmp);
    DeleteDC(dc);

    ICONINFO ii = { 0 };
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = color;
    HICON icon  = CreateIconIndirect(&ii);            /* copies the bitmaps */

    DeleteObject(color);
    DeleteObject(mask);
    return icon ? icon : LoadIconA(NULL, IDI_APPLICATION);
}

/* Rebuild the tray icon from a snapshot and swap it in. */
static void update_icon(const Snapshot *s)
{
    HICON icon = build_stripe_icon(s->on, s->non > 0 ? s->non : g_cfg.channels);
    g_nid.uFlags = NIF_ICON;
    g_nid.hIcon  = icon;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
    if (g_cur_icon) DestroyIcon(g_cur_icon);
    g_cur_icon = icon;
    g_nid.uFlags = NIF_TIP;
}

static void show_menu(HWND h)
{
    Snapshot s;
    EnterCriticalSection(&g_lock);
    s = g_snap;
    LeaveCriticalSection(&g_lock);

    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING | MF_DISABLED, 0, s.header[0] ? s.header : "psuctrl");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);

    if (s.nlines == 0)
        AppendMenuA(m, MF_STRING | MF_DISABLED, 0, "(no channel data)");
    for (int i = 0; i < s.nlines; ++i)
        AppendMenuA(m, MF_STRING | MF_DISABLED, 0, s.lines[i]);

    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, ID_STATUS, BUILTIN_STATUS);
    for (int i = 0; i < g_ncmds; ++i)
        AppendMenuA(m, MF_STRING, ID_CMD_BASE + i, g_cmds[i].label);
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);

    AppendMenuA(m, MF_STRING, ID_RECONNECT, g_cfg.mock ? "Reset mock" : "Reconnect");
    AppendMenuA(m, MF_STRING, ID_RELOAD,    "Reload config");
    AppendMenuA(m, MF_STRING, ID_OPENCFG,   "Edit config file");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, ID_EXIT,      "Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             pt.x, pt.y, 0, h, NULL);
    DestroyMenu(m);
    PostMessage(h, WM_NULL, 0, 0);

    if (cmd >= ID_CMD_BASE && cmd < ID_CMD_BASE + g_ncmds) {
        EnterCriticalSection(&g_lock);
        g_pending = cmd - ID_CMD_BASE;
        LeaveCriticalSection(&g_lock);
        SetEvent(g_wake);
    } else if (cmd == ID_STATUS) {
        EnterCriticalSection(&g_lock);
        g_pending = PEND_STATUS;
        LeaveCriticalSection(&g_lock);
        SetEvent(g_wake);
    } else if (cmd == ID_RECONNECT) {
        if (g_cfg.mock) mock_init();
        else { g_reconnect = 1; SetEvent(g_wake); }
    } else if (cmd == ID_RELOAD) {
        g_reload = 1; SetEvent(g_wake);
    } else if (cmd == ID_OPENCFG) {
        ShellExecuteA(NULL, "open", "notepad.exe", g_cfgpath, NULL, SW_SHOW);
    } else if (cmd == ID_EXIT) {
        DestroyWindow(h);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU)
            show_menu(h);
        return 0;

    case WM_REFRESH_TIP: {
        Snapshot s;
        EnterCriticalSection(&g_lock);
        s = g_snap;
        LeaveCriticalSection(&g_lock);
        snprintf(g_nid.szTip, sizeof g_nid.szTip, "%s", s.tip[0] ? s.tip : "psuctrl");
        g_nid.uFlags = NIF_TIP;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
        update_icon(&s);
        return 0;
    }

    case WM_BALLOON: {
        char ti[64], tx[256];
        EnterCriticalSection(&g_lock);
        snprintf(ti, sizeof ti, "%s", g_balloon.title);
        snprintf(tx, sizeof tx, "%s", g_balloon.text);
        LeaveCriticalSection(&g_lock);
        g_nid.uFlags = NIF_INFO;
        snprintf(g_nid.szInfo,      sizeof g_nid.szInfo,      "%s", tx);
        snprintf(g_nid.szInfoTitle, sizeof g_nid.szInfoTitle, "%s", ti);
        g_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
        g_nid.uFlags = NIF_TIP;
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, w, l);
}

/* =================================================================== */
/*  headless CLI (for scripting: python subprocess, batch files, ...)    */
/* =================================================================== */
enum { CLI_NONE = 0, CLI_RUN, CLI_STATUS, CLI_LIST, CLI_HELP };

static const char *CLI_USAGE =
    "psuctrl - VISA power-supply tray controller\n"
    "\n"
    "  psuctrl [--mock] [--config PATH]              run as a tray icon\n"
    "  psuctrl --list [--config PATH]                list command labels\n"
    "  psuctrl --status [--mock] [--config PATH]     print per-channel status lines\n"
    "  psuctrl --run \"LABEL\" [--mock] [--config PATH] run one command sequence\n"
    "\n"
    "\"status\" is a built-in --run label: one line per channel with the output\n"
    "state and measured voltage. For a no-argument dump of output / vset / vmeas\n"
    "/ imeas, use the separate psuctrl_status.exe.\n"
    "\n"
    "exit codes: 0 ok, 2 bad usage / unknown label, 3 no VISA runtime,\n"
    "            4 no instrument, 5 instrument did not respond\n";

static int cli_find_cmd(const char *label)
{
    for (int i = 0; i < g_ncmds; ++i)
        if (_stricmp(g_cmds[i].label, label) == 0) return i;
    return -1;
}

static int cli_main(int action, const char *arg)
{
    cli_stdio();

    if (action == CLI_HELP) { fputs(CLI_USAGE, stdout); return 0; }

    if (!config_load(g_cfgpath)) {
        config_write_default(g_cfgpath);
        config_load(g_cfgpath);
    }
    if (g_force_mock) g_cfg.mock = 1;

    if (action == CLI_LIST) {
        printf("%s\n", BUILTIN_STATUS);
        for (int i = 0; i < g_ncmds; ++i) printf("%s\n", g_cmds[i].label);
        fflush(stdout);
        return 0;
    }

    if (g_cfg.mock) {
        mock_init();
    } else {
        if (!load_visa()) {
            fprintf(stderr, "psuctrl: visa32.dll not found (VISA runtime installed?)\n");
            return 3;
        }
        g_visa_loaded = 1;
        if (!visa_connect()) {
            fprintf(stderr, "psuctrl: no VISA instrument found\n");
            return 4;
        }
    }

    int rc = 0;
    if (action == CLI_RUN) {
        char out[512];
        int idx;
        if (_stricmp(arg, BUILTIN_STATUS) == 0) {
            rc = builtin_status(out, sizeof out) ? 5 : 0;
            if (out[0]) printf("%s\n", out);
        } else if ((idx = cli_find_cmd(arg)) >= 0) {
            rc = run_sequence(g_cmds[idx].seq, out, sizeof out) ? 5 : 0;
            if (out[0]) printf("%s\n", out);
        } else {
            fprintf(stderr, "psuctrl: unknown command \"%s\"; available labels:\n  %s\n",
                    arg, BUILTIN_STATUS);
            for (int i = 0; i < g_ncmds; ++i) fprintf(stderr, "  %s\n", g_cmds[i].label);
            rc = 2;
        }
    } else { /* CLI_STATUS */
        int n = g_cfg.channels;
        for (int ch = 1; ch <= n; ++ch) {
            char line[192];
            if (!expand_line(g_cfg.status_tmpl, ch, line, sizeof line)) rc = 5;
            printf("%s\n", line);
        }
    }

    fflush(stdout);
    if (!g_cfg.mock) visa_close();
    return rc;
}

/* =================================================================== */
/*  entry point                                                         */
/* =================================================================== */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show)
{
    (void)hPrev; (void)cmdline; (void)show;

    srand((unsigned)time(NULL));
    mock_init();

    get_exe_dir(g_exedir, sizeof g_exedir);
    snprintf(g_cfgpath, sizeof g_cfgpath, "%s\\psuctrl.ini", g_exedir);

    int cli_action = CLI_NONE;
    const char *cli_arg = NULL;
    for (int i = 1; i < __argc; ++i) {
        const char *a = __argv[i];
        if (!strcmp(a, "--mock") || !strcmp(a, "-m")) {
            g_force_mock = 1;
        } else if ((!strcmp(a, "--config") || !strcmp(a, "-c")) && i + 1 < __argc) {
            snprintf(g_cfgpath, sizeof g_cfgpath, "%s", __argv[++i]);
        } else if (!strcmp(a, "--run") && i + 1 < __argc) {
            cli_action = CLI_RUN; cli_arg = __argv[++i];
        } else if (!strcmp(a, "--status")) {
            cli_action = CLI_STATUS;
        } else if (!strcmp(a, "--list")) {
            cli_action = CLI_LIST;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            cli_action = CLI_HELP;
        }
    }

    if (cli_action != CLI_NONE)
        return cli_main(cli_action, cli_arg);

    HANDLE mtx = CreateMutexA(NULL, TRUE, "psuctrl_tray_singleton");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(NULL, "psuctrl is already running.", "psuctrl", MB_ICONINFORMATION);
        return 0;
    }

    InitializeCriticalSection(&g_lock);

    if (!config_load(g_cfgpath)) {
        config_write_default(g_cfgpath);
        config_load(g_cfgpath);
    }
    if (g_force_mock) g_cfg.mock = 1;

    if (!g_cfg.mock)
        g_visa_loaded = load_visa();

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "psuctrl_wnd";
    RegisterClassA(&wc);
    g_hwnd = CreateWindowA("psuctrl_wnd", "psuctrl", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, hInst, NULL);

    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize           = sizeof g_nid;
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    {
        int off[MAX_CHANNELS] = {0};
        g_cur_icon = build_stripe_icon(off, g_cfg.channels);
    }
    g_nid.hIcon = g_cur_icon;
    snprintf(g_nid.szTip, sizeof g_nid.szTip, "psuctrl - starting...");
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    g_wake   = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_worker = CreateThread(NULL, 0, worker, NULL, 0, NULL);

    MSG m;
    while (GetMessage(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    g_quit = 1;
    SetEvent(g_wake);
    WaitForSingleObject(g_worker, 3000);
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    if (g_cur_icon) DestroyIcon(g_cur_icon);
    if (!g_cfg.mock) visa_close();
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    DeleteCriticalSection(&g_lock);
    return 0;
}
