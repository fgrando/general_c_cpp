/*
 * banner.c -- generic signal-file monitor with a screen-wide banner.
 *
 * Watches one file. While that file exists a full-width banner is docked to the
 * top of the primary monitor showing a configurable message, and the tray icon
 * turns red. When the file is gone the banner disappears and the icon is green.
 * No network, no credentials -- just a local stat every couple of seconds.
 *
 * Two indications, because a tray icon alone is easy to miss and its
 * visibility is a per-user Explorer setting we do not fully control:
 *
 *   1. tray icon + menu (detail, on demand)
 *   2. a full-width banner docked to the top of the screen while the file is
 *      present. Registered as an appbar, so maximized windows cannot cover it,
 *      and WS_EX_NOACTIVATE so it never steals focus from the foreground app.
 *
 * Configuration -- an .ini file next to the exe (same basename, e.g.
 * banner.exe -> banner.ini), or a path passed as the first command-line
 * argument. Simple "key = value" lines; a line whose first non-blank character
 * is '#' or ';' is a comment. Keys:
 *
 *   signal_file     path to the file to watch                 (required)
 *   message         text shown in the banner while present    (default "In use")
 *   title           name used for the tray tooltip and menu   (default "File monitor")
 *   poll_ms         poll interval in milliseconds             (default 2000, min 100)
 *   banner_on_free  1 = also show a green banner when the file is absent
 *
 * If the first line of the signal file is non-empty it is appended to the
 * banner message, so whatever creates the file can leave a live detail string
 * in it. That is optional -- an empty file works fine.
 *
 * The tray icon uses a fixed GUID (NIF_GUID). Per Microsoft's NOTIFYICONDATA
 * documentation that GUID is bound to the BINARY PATH: "The path of the binary
 * file is included in the registration of the icon's GUID and cannot be
 * changed." Move or rename this exe after first run and Shell_NotifyIcon will
 * FAIL, not degrade. Pick the install path once and leave it there; if you
 * must move it, change TRAY_GUID at the same time.
 *
 * Build (MinGW-w64):
 *   x86_64-w64-mingw32-gcc -O0 -g -Wall -Wextra -municode -mwindows \
 *       -o banner.exe banner.c -lshell32 -lgdi32 -luser32
 */

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= DEFAULTS ======================= */
#define DEF_MESSAGE     "In use"
#define DEF_TITLE       "File monitor"
#define DEF_POLL_MS     2000    /* sentinel poll; a local stat, essentially free */
#define MIN_POLL_MS     100
#define BANNER_H        34      /* logical px, scaled by DPI at runtime          */

/* Regenerate this if you ever move the exe. It is bound to the binary path. */
static const GUID TRAY_GUID =
    { 0x7c2f4e10, 0x9b3a, 0x4d6e, { 0xa1, 0xc5, 0x3f, 0x8b, 0x2d, 0x6e, 0x4a, 0x97 } };
/* ======================================================= */

#define WM_TRAY      (WM_APP + 1)
#define WM_APPBAR    (WM_APP + 2)
#define ID_STATUS    1001
#define ID_DETAIL    1002
#define ID_REVEAL    1003
#define ID_REFRESH   1004
#define ID_BANNER    1005
#define ID_RELOAD    1006
#define ID_EXIT      1007
#define TIMER_POLL   1

typedef enum { ST_FREE = 0, ST_BUSY } state_t;

static HWND     g_tray_wnd, g_banner;
static NOTIFYICONDATAA g_nid;
static HICON    g_ico[2];
static HFONT    g_font;
static UINT     g_taskbar_created;
static state_t  g_state = ST_FREE;
static int      g_started = 0;      /* suppress the balloon on first poll */
static int      g_banner_enabled = 1;
static int      g_appbar_registered = 0;
static int      g_banner_h = BANNER_H;
static char     g_status[256]  = "Signal file not present";
static char     g_detail[200]  = "";

/* config, loaded from the .ini */
static char     g_cfg_path[MAX_PATH] = "";
static char     g_signal_path[MAX_PATH] = "";
static char     g_message[256] = DEF_MESSAGE;
static char     g_title[64]    = DEF_TITLE;
static int      g_poll_ms      = DEF_POLL_MS;
static int      g_banner_on_free = 0;

/* ---------------------------------------------------------------- config */

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n > 0 && (unsigned char)s[n - 1] <= ' ') s[--n] = 0;
    while (s[i] && (unsigned char)s[i] <= ' ') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

static void unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = 0;
        memmove(s, s + 1, n - 1);
    }
}

typedef struct {
    char signal[MAX_PATH];
    char message[256];
    char title[64];
    int  poll_ms;
    int  banner_on_free;
} cfg_t;

/* Returns 1 if the file parsed and signal_file was set, 0 otherwise. */
static int parse_config(cfg_t *c)
{
    FILE *f;
    char line[512];

    lstrcpynA(c->message, DEF_MESSAGE, sizeof c->message);
    lstrcpynA(c->title, DEF_TITLE, sizeof c->title);
    c->poll_ms = DEF_POLL_MS;
    c->banner_on_free = 0;
    c->signal[0] = 0;

    f = fopen(g_cfg_path, "rb");
    if (!f) return 0;

    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *val, key[64];

        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == ';' || *s == '\r' || *s == '\n' || *s == 0)
            continue;

        eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        val = eq + 1;

        lstrcpynA(key, s, sizeof key);
        trim(key);
        trim(val);

        if (!_stricmp(key, "signal_file"))          lstrcpynA(c->signal, val, sizeof c->signal);
        else if (!_stricmp(key, "message"))         lstrcpynA(c->message, val, sizeof c->message);
        else if (!_stricmp(key, "title"))           lstrcpynA(c->title, val, sizeof c->title);
        else if (!_stricmp(key, "poll_ms"))         { int v = atoi(val); if (v >= MIN_POLL_MS) c->poll_ms = v; }
        else if (!_stricmp(key, "banner_on_free"))  c->banner_on_free = atoi(val) != 0;
    }
    fclose(f);
    return c->signal[0] != 0;
}

/* Parse into temporaries and only commit on success, so a bad reload at
 * runtime leaves the running configuration untouched. */
static int load_config(void)
{
    cfg_t c;
    if (!parse_config(&c)) return 0;
    lstrcpynA(g_signal_path, c.signal, sizeof g_signal_path);
    lstrcpynA(g_message, c.message, sizeof g_message);
    lstrcpynA(g_title, c.title, sizeof g_title);
    g_poll_ms = c.poll_ms;
    g_banner_on_free = c.banner_on_free;
    return 1;
}

static void write_default_config(void)
{
    FILE *f = fopen(g_cfg_path, "wb");
    if (!f) return;
    fputs("# banner.exe -- signal-file monitor configuration\n"
          "#\n"
          "# While signal_file exists, a screen-wide banner shows 'message'.\n"
          "# Lines starting with '#' or ';' are comments.\n"
          "\n"
          "signal_file   = C:\\path\\to\\signal.lock\n"
          "message       = Resource in use\n"
          "title         = File monitor\n"
          "poll_ms       = 2000\n"
          "banner_on_free = 0\n", f);
    fclose(f);
}

/* Resolve g_cfg_path: first CLI arg if given, else <exe basename>.ini. */
static void resolve_cfg_path(PWSTR cmd)
{
    char *dot, *slash;

    if (cmd && cmd[0]) {
        WideCharToMultiByte(CP_ACP, 0, cmd, -1, g_cfg_path, sizeof g_cfg_path, NULL, NULL);
        g_cfg_path[sizeof g_cfg_path - 1] = 0;
        trim(g_cfg_path);
        unquote(g_cfg_path);
        if (g_cfg_path[0]) return;
    }

    GetModuleFileNameA(NULL, g_cfg_path, sizeof g_cfg_path);
    g_cfg_path[sizeof g_cfg_path - 1] = 0;
    slash = strrchr(g_cfg_path, '\\');
    dot = strrchr(g_cfg_path, '.');
    if (dot && (!slash || dot > slash))
        lstrcpynA(dot, ".ini", (int)(sizeof g_cfg_path - (dot - g_cfg_path)));
    else
        lstrcatA(g_cfg_path, ".ini");
}

/* ---------------------------------------------------------------- icons */

static HICON make_dot(COLORREF c)
{
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HDC mdc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, NULL);
    HBITMAP oldb = (HBITMAP)SelectObject(dc, bmp);
    HBITMAP oldm = (HBITMAP)SelectObject(mdc, mask);
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
    HBRUSH oldbr;
    HPEN oldpen;
    RECT r = { 0, 0, 16, 16 };
    ICONINFO ii;
    HICON ic;

    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    oldbr = (HBRUSH)SelectObject(dc, br);
    oldpen = (HPEN)SelectObject(dc, pen);
    Ellipse(dc, 1, 1, 15, 15);
    SelectObject(dc, oldbr);
    SelectObject(dc, oldpen);

    /* mask: 1 = transparent, 0 = opaque */
    PatBlt(mdc, 0, 0, 16, 16, WHITENESS);
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    Ellipse(mdc, 1, 1, 15, 15);

    ii.fIcon = TRUE; ii.xHotspot = 0; ii.yHotspot = 0;
    ii.hbmMask = mask; ii.hbmColor = bmp;
    ic = CreateIconIndirect(&ii);

    SelectObject(dc, oldb);
    SelectObject(mdc, oldm);
    DeleteObject(br); DeleteObject(pen);
    DeleteObject(bmp); DeleteObject(mask);
    DeleteDC(dc); DeleteDC(mdc);
    ReleaseDC(NULL, screen);
    return ic;
}

/* ------------------------------------------------------------- appbar */

static void appbar_remove(void)
{
    APPBARDATA ab;
    if (!g_appbar_registered) return;
    memset(&ab, 0, sizeof(ab));
    ab.cbSize = sizeof(ab);
    ab.hWnd = g_banner;
    SHAppBarMessage(ABM_REMOVE, &ab);
    g_appbar_registered = 0;
}

/* Reserve a strip at the top of the primary monitor so maximized windows are
 * pushed down instead of covering the banner. */
static void appbar_place(void)
{
    APPBARDATA ab;
    int w = GetSystemMetrics(SM_CXSCREEN);

    memset(&ab, 0, sizeof(ab));
    ab.cbSize = sizeof(ab);
    ab.hWnd = g_banner;

    if (!g_appbar_registered) {
        ab.uCallbackMessage = WM_APPBAR;
        if (!SHAppBarMessage(ABM_NEW, &ab)) return;
        g_appbar_registered = 1;
    }

    ab.uEdge = ABE_TOP;
    ab.rc.left = 0;
    ab.rc.right = w;
    ab.rc.top = 0;
    ab.rc.bottom = g_banner_h;
    SHAppBarMessage(ABM_QUERYPOS, &ab);
    ab.rc.bottom = ab.rc.top + g_banner_h;
    SHAppBarMessage(ABM_SETPOS, &ab);

    MoveWindow(g_banner, ab.rc.left, ab.rc.top,
               ab.rc.right - ab.rc.left, ab.rc.bottom - ab.rc.top, TRUE);
}

static void banner_show(int on)
{
    if (on && g_banner_enabled) {
        appbar_place();
        /* SWP_NOACTIVATE: never pull focus away from whatever the user is
         * doing -- a stolen focus mid-task is its own kind of outage. */
        SetWindowPos(g_banner, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(g_banner, NULL, TRUE);
    } else {
        ShowWindow(g_banner, SW_HIDE);
        appbar_remove();
    }
}

/* ---------------------------------------------------------------- tray */

static void tray_add(void)
{
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_tray_wnd;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_GUID;
    g_nid.guidItem = TRAY_GUID;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_ico[g_state];
    lstrcpynA(g_nid.szTip, g_title, sizeof(g_nid.szTip));

    if (!Shell_NotifyIconA(NIM_ADD, &g_nid)) {
        /* A previous instance that died without NIM_DELETE leaves the GUID
         * registered; clearing it and retrying is the documented recovery. */
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        Shell_NotifyIconA(NIM_ADD, &g_nid);
    }
}

static void tray_update(void)
{
    char tip[128];
    /* szTip is 128 bytes and anything longer is silently dropped by the shell,
     * so cap each field with an explicit precision -- 48 + 2 + 72 < 128. */
    snprintf(tip, sizeof(tip), "%.48s: %.72s", g_title, g_status);
    lstrcpynA(g_nid.szTip, tip, sizeof(g_nid.szTip));
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_GUID;
    g_nid.hIcon = g_ico[g_state];
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void balloon(const char *title, const char *text)
{
    NOTIFYICONDATAA n = g_nid;
    n.uFlags = NIF_INFO | NIF_GUID;
    n.dwInfoFlags = NIIF_INFO;
    n.uTimeout = 8000;
    lstrcpynA(n.szInfoTitle, title, sizeof(n.szInfoTitle));
    lstrcpynA(n.szInfo, text, sizeof(n.szInfo));
    Shell_NotifyIconA(NIM_MODIFY, &n);
}

/* --------------------------------------------------------------- state */

/* First line of the signal file -- whatever was written there, if anything. */
static void read_detail(void)
{
    HANDLE h;
    char buf[200];
    DWORD got = 0;
    size_t i;

    g_detail[0] = 0;
    h = CreateFileA(g_signal_path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) && got > 0) {
        buf[got] = 0;
        for (i = 0; i < got; i++) {
            if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
            if ((unsigned char)buf[i] < 32) buf[i] = ' ';
        }
        lstrcpynA(g_detail, buf, sizeof(g_detail));
    }
    CloseHandle(h);
}

static void poll(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    state_t was = g_state;

    if (GetFileAttributesExA(g_signal_path, GetFileExInfoStandard, &fad)) {
        read_detail();
        g_state = ST_BUSY;
        lstrcpynA(g_status, g_message, sizeof(g_status));
    } else {
        g_state = ST_FREE;
        g_detail[0] = 0;
        lstrcpynA(g_status, "Signal file not present", sizeof(g_status));
    }

    tray_update();

    if (g_state != was || !g_started) {
        banner_show(g_state != ST_FREE || g_banner_on_free);
        InvalidateRect(g_banner, NULL, TRUE);
        if (g_started && g_state != was) {
            if (g_state == ST_BUSY)
                balloon(g_title, g_message);
            else
                balloon(g_title, "Signal file cleared.");
        }
    }
    g_started = 1;
}

/* -------------------------------------------------------------- banner */

static LRESULT CALLBACK banner_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc, tr;
        COLORREF bg = (g_state == ST_BUSY) ? RGB(178, 34, 34)
                                           : RGB(30, 120, 60);
        HBRUSH br = CreateSolidBrush(bg);
        char line[512];
        HFONT oldf;

        GetClientRect(h, &rc);
        FillRect(dc, &rc, br);
        DeleteObject(br);

        if (g_detail[0])
            snprintf(line, sizeof(line), "%s  -  %s", g_status, g_detail);
        else
            snprintf(line, sizeof(line), "%s", g_status);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        oldf = (HFONT)SelectObject(dc, g_font);
        tr = rc;
        DrawTextA(dc, line, -1, &tr,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, oldf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_APPBAR:
        /* Taskbar moved or a full-screen app appeared: re-assert our strip. */
        if (wp == ABN_POSCHANGED) appbar_place();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (IsWindowVisible(h)) appbar_place();
        return 0;
    case WM_DESTROY:
        appbar_remove();
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- menu */

static void show_menu(void)
{
    HMENU m = CreatePopupMenu();
    POINT p;

    AppendMenuA(m, MF_STRING | MF_GRAYED, ID_STATUS, g_status);
    if (g_detail[0])
        AppendMenuA(m, MF_STRING | MF_GRAYED, ID_DETAIL, g_detail);
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, ID_REVEAL, "Show signal file in Explorer");
    AppendMenuA(m, MF_STRING, ID_REFRESH, "Refresh now");
    AppendMenuA(m, MF_STRING, ID_RELOAD, "Reload config");
    AppendMenuA(m, MF_STRING | (g_banner_enabled ? MF_CHECKED : 0),
                ID_BANNER, "Show banner when detected");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, ID_EXIT, "Exit");

    GetCursorPos(&p);
    /* SetForegroundWindow + the trailing post are the documented dance that
     * makes a tray menu dismiss when the user clicks elsewhere. */
    SetForegroundWindow(g_tray_wnd);
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, p.x, p.y, 0, g_tray_wnd, NULL);
    PostMessageA(g_tray_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK tray_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_taskbar_created) {
        /* Explorer restarted: every tray icon is gone, re-add ours. */
        tray_add();
        tray_update();
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) show_menu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_REVEAL: {
            char param[MAX_PATH + 16];
            snprintf(param, sizeof(param), "/select,\"%s\"", g_signal_path);
            param[sizeof(param) - 1] = 0;
            ShellExecuteA(NULL, "open", "explorer.exe", param, NULL, SW_SHOWNORMAL);
            break;
        }
        case ID_REFRESH:
            poll();
            break;
        case ID_RELOAD:
            if (load_config()) {
                KillTimer(g_tray_wnd, TIMER_POLL);
                SetTimer(g_tray_wnd, TIMER_POLL, g_poll_ms, NULL);
                g_started = 0;          /* re-evaluate from scratch */
                poll();
                balloon(g_title, "Configuration reloaded.");
            } else {
                balloon(g_title, "Config reload failed; keeping current settings.");
            }
            break;
        case ID_BANNER:
            g_banner_enabled = !g_banner_enabled;
            banner_show(g_state != ST_FREE || g_banner_on_free);
            break;
        case ID_EXIT:
            DestroyWindow(h);
            break;
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_POLL) poll();
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        if (g_banner) DestroyWindow(g_banner);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- main */

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    HANDLE once;
    HDC screen;
    int dpi;

    (void)hp; (void)show;

    resolve_cfg_path(cmd);

    if (GetFileAttributesA(g_cfg_path) == INVALID_FILE_ATTRIBUTES) {
        char text[MAX_PATH + 160];
        write_default_config();
        snprintf(text, sizeof(text),
                 "No configuration found, so a template was written to:\n\n%s\n\n"
                 "Set signal_file (and message) in it, then run this again.",
                 g_cfg_path);
        MessageBoxA(NULL, text, DEF_TITLE, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (!load_config()) {
        char text[MAX_PATH + 160];
        snprintf(text, sizeof(text),
                 "'signal_file' is not set in:\n\n%s\n\nNothing to monitor.",
                 g_cfg_path);
        MessageBoxA(NULL, text, DEF_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }

    once = CreateMutexA(NULL, TRUE, "Local\\banner_filemon");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_taskbar_created = RegisterWindowMessageA("TaskbarCreated");

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = tray_proc;
    wc.hInstance = hi;
    wc.lpszClassName = "filemon_wnd";
    RegisterClassA(&wc);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = banner_proc;
    wc.hInstance = hi;
    wc.lpszClassName = "filemon_banner";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    /* Message-only window: no taskbar button, nothing visible. */
    g_tray_wnd = CreateWindowExA(0, "filemon_wnd", "filemon", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, NULL, hi, NULL);
    if (!g_tray_wnd) return 1;

    screen = GetDC(NULL);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    g_banner_h = MulDiv(BANNER_H, dpi, 96);
    g_font = CreateFontA(-MulDiv(15, dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    /* WS_EX_NOACTIVATE keeps focus where it is; TOOLWINDOW keeps it out of
     * Alt-Tab; TOPMOST plus the appbar keeps it above maximized windows. */
    g_banner = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "filemon_banner", "Signal file status", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), g_banner_h,
        NULL, NULL, hi, NULL);
    if (!g_banner) return 1;

    g_ico[ST_FREE] = make_dot(RGB(40, 170, 70));
    g_ico[ST_BUSY] = make_dot(RGB(200, 50, 50));

    tray_add();
    poll();
    SetTimer(g_tray_wnd, TIMER_POLL, g_poll_ms, NULL);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    appbar_remove();
    if (g_font) DeleteObject(g_font);
    if (once) CloseHandle(once);
    return 0;
}
