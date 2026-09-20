/* filewatch.c - poll a path (wildcards allowed); while a matching file
 * exists, show a notification and dock a full-width banner to the top of
 * the primary monitor. Registered as an appbar (SHAppBarMessage) so
 * maximized windows are pushed down instead of covering it, and
 * WS_EX_NOACTIVATE so it never steals focus. Re-arms once the file is
 * gone, with a second notification. Behavior adapted from ../banner/banner.c.
 *
 * If the matched file's first line is non-empty it is shown alongside the
 * message, so whatever creates the file can leave a live detail string in
 * it (optional - an empty file works fine).
 */
#include "app.h"
#include <stdio.h>
#include <string.h>

/* Module identity: name in the Module struct, ini section, alert source. */
#define MOD "filewatch"
#define BANNER_H 34             /* logical px, scaled by DPI at runtime */
#define WM_APPBAR (WM_APP + 2)  /* private to the banner window */

/* Menu item IDs, as offsets from the base the host hands us. */
enum {
    CMD_TOGGLE_WATCH = 1,
    CMD_REFRESH,
    CMD_RELOAD,
    CMD_TOGGLE_BANNER,
    CMD_REVEAL
};

static const HostAPI *host;
static char path[MAX_PATH], message[128], found[MAX_PATH];
static char status[160], detail[200];
static int  enabled, interval, counter, triggered, started;
static int  banner_on_free, banner_enabled = 1;
static HWND banner;
static HFONT font;
static int  banner_h = BANNER_H;
static int  appbar_registered;

/* ---------------------------------------------------------------- helpers */

/* dir(path) + found -> full path of the currently matched file. */
static void build_found_path(char *out, size_t outsz)
{
    const char *slash = strrchr(path, '\\');
    if (slash != NULL) {
        snprintf(out, outsz, "%.*s%s", (int)(slash - path + 1), path, found);
    } else {
        snprintf(out, outsz, "%s", found);
    }
}

/* First line of the matched file, if any - whatever was written there. */
static void read_detail(const char *full_path)
{
    HANDLE h;
    char buf[200];
    DWORD got = 0;
    size_t i;

    detail[0] = '\0';
    h = CreateFileA(full_path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) && got > 0) {
        buf[got] = '\0';
        for (i = 0; i < got; i++) {
            if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = '\0'; break; }
            if ((unsigned char)buf[i] < 32) buf[i] = ' ';
        }
        lstrcpynA(detail, buf, sizeof(detail));
    }
    CloseHandle(h);
}

/* ------------------------------------------------------------- appbar */

static void appbar_remove(void)
{
    APPBARDATA ab;
    if (!appbar_registered) return;
    memset(&ab, 0, sizeof(ab));
    ab.cbSize = sizeof(ab);
    ab.hWnd = banner;
    SHAppBarMessage(ABM_REMOVE, &ab);
    appbar_registered = 0;
}

/* Reserve a strip at the top of the primary monitor so maximized windows
 * are pushed down instead of covering the banner. */
static void appbar_place(void)
{
    APPBARDATA ab;
    int w = GetSystemMetrics(SM_CXSCREEN);

    memset(&ab, 0, sizeof(ab));
    ab.cbSize = sizeof(ab);
    ab.hWnd = banner;

    if (!appbar_registered) {
        ab.uCallbackMessage = WM_APPBAR;
        if (!SHAppBarMessage(ABM_NEW, &ab)) return;
        appbar_registered = 1;
    }

    ab.uEdge = ABE_TOP;
    ab.rc.left = 0;
    ab.rc.right = w;
    ab.rc.top = 0;
    ab.rc.bottom = banner_h;
    SHAppBarMessage(ABM_QUERYPOS, &ab);
    ab.rc.bottom = ab.rc.top + banner_h;
    SHAppBarMessage(ABM_SETPOS, &ab);

    MoveWindow(banner, ab.rc.left, ab.rc.top,
               ab.rc.right - ab.rc.left, ab.rc.bottom - ab.rc.top, TRUE);
}

static void banner_show(int on)
{
    if (banner == NULL) return;   /* window creation failed at init */
    if (on && banner_enabled) {
        appbar_place();
        /* SWP_NOACTIVATE: never pull focus away from whatever the user is
         * doing. */
        SetWindowPos(banner, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(banner, NULL, TRUE);
    } else {
        ShowWindow(banner, SW_HIDE);
        appbar_remove();
    }
}

static LRESULT CALLBACK banner_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        COLORREF bg = triggered ? RGB(178, 34, 34) : RGB(30, 120, 60);
        HBRUSH br = CreateSolidBrush(bg);
        HFONT oldf;
        char line[sizeof(status) + sizeof(detail) + 8];

        GetClientRect(h, &rc);
        FillRect(dc, &rc, br);
        DeleteObject(br);

        if (detail[0]) snprintf(line, sizeof(line), "%s  -  %s", status, detail);
        else            snprintf(line, sizeof(line), "%s", status);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        oldf = (HFONT)SelectObject(dc, font);
        DrawTextA(dc, line, -1, &rc,
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
    default:
        break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* --------------------------------------------------------------- state */

static void filewatch_load_config(void)
{
    enabled  = host->ini_int(MOD, "enabled", 1);
    interval = host->ini_int(MOD, "interval", 2);
    if (interval < 1) interval = 1;
    host->ini_str(MOD, "path", "C:\\temp\\*.trigger", path, sizeof(path));
    host->ini_str(MOD, "message", "File detected", message, sizeof(message));
    banner_on_free = host->ini_int(MOD, "banner_on_free", 0);
}

static void filewatch_poll(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE f;
    int exists = 0;
    int was = triggered;

    if (enabled) {
        f = FindFirstFileA(path, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    exists = 1;
                    lstrcpynA(found, fd.cFileName, sizeof(found));
                    break;
                }
            } while (FindNextFileA(f, &fd));
            FindClose(f);
        }
    }

    if (exists) {
        char full[MAX_PATH * 2];
        build_found_path(full, sizeof(full));
        read_detail(full);
        triggered = 1;
        lstrcpynA(status, message, sizeof(status));
    } else {
        triggered = 0;
        detail[0] = '\0';
        lstrcpynA(status, "Signal file not present", sizeof(status));
    }

    host->set_alert(MOD, triggered);

    if (triggered != was || !started) {
        banner_show(triggered || banner_on_free);
        if (started && triggered != was) {
            if (triggered) host->notify("File watch", message, NIIF_WARNING);
            else           host->notify("File watch", "Signal file cleared.", NIIF_INFO);
        }
    }
    started = 1;
}

/* ---------------------------------------------------------------- module */

static void filewatch_init(const HostAPI *h)
{
    WNDCLASSA wc;
    HDC screen;
    int dpi;

    host = h;
    filewatch_load_config();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = banner_proc;
    wc.hInstance = host->inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "TrayCtlBanner";
    RegisterClassA(&wc);

    screen = GetDC(NULL);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    banner_h = MulDiv(BANNER_H, dpi, 96);
    font = CreateFontA(-MulDiv(15, dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

    /* WS_EX_NOACTIVATE keeps focus where it is; TOOLWINDOW keeps it out of
     * Alt-Tab; TOPMOST plus the appbar keeps it above maximized windows. */
    banner = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "TrayCtlBanner", "", WS_POPUP, 0, 0,
        GetSystemMetrics(SM_CXSCREEN), banner_h, NULL, NULL, host->inst, NULL);
}

static void filewatch_tick(void)
{
    if (!enabled || ++counter < interval) return;
    counter = 0;
    filewatch_poll();
}

static void filewatch_menu(HMENU menu, UINT base)
{
    char text[MAX_PATH + 32];

    snprintf(text, sizeof(text), "Watch: %s", path);
    AppendMenuA(menu, MF_STRING | (enabled ? MF_CHECKED : 0),
                base + CMD_TOGGLE_WATCH, text);
    if (!enabled) return;

    /* Grayed items can't be clicked, so their ID is never dispatched. */
    AppendMenuA(menu, MF_STRING | MF_GRAYED, base, status);
    if (detail[0]) AppendMenuA(menu, MF_STRING | MF_GRAYED, base, detail);
    AppendMenuA(menu, MF_STRING, base + CMD_REFRESH, "Refresh now");
    AppendMenuA(menu, MF_STRING, base + CMD_RELOAD, "Reload config");
    AppendMenuA(menu, MF_STRING | (banner_enabled ? MF_CHECKED : 0),
                base + CMD_TOGGLE_BANNER, "Show banner when detected");
    if (triggered) {
        AppendMenuA(menu, MF_STRING, base + CMD_REVEAL,
                    "Show signal file in Explorer");
    }
}

static void filewatch_command(UINT off)
{
    switch (off) {
    case CMD_TOGGLE_WATCH:
        enabled = !enabled;
        if (enabled) {
            counter = 0;
            started = 0;       /* re-evaluate from scratch */
            filewatch_poll();
        } else {
            triggered = 0;
            detail[0] = '\0';
            host->set_alert(MOD, 0);
            banner_show(0);
        }
        break;
    case CMD_REFRESH:
        filewatch_poll();
        break;
    case CMD_RELOAD:
        filewatch_load_config();
        counter = 0;
        started = 0;
        filewatch_poll();
        host->notify("File watch", "Configuration reloaded.", NIIF_INFO);
        break;
    case CMD_TOGGLE_BANNER:
        banner_enabled = !banner_enabled;
        banner_show(triggered || banner_on_free);
        break;
    case CMD_REVEAL: {
        char full[MAX_PATH * 2], param[MAX_PATH * 2 + 16];
        build_found_path(full, sizeof(full));
        snprintf(param, sizeof(param), "/select,\"%s\"", full);
        ShellExecuteA(NULL, "open", "explorer.exe", param, NULL, SW_SHOWNORMAL);
        break;
    }
    default:
        break;
    }
}

static void filewatch_tip(char *buf, int size)
{
    if (!enabled) return;
    snprintf(buf, (size_t)size, "Watch: %s", status);
}

static void filewatch_shutdown(void)
{
    appbar_remove();
    if (banner != NULL) DestroyWindow(banner);
    if (font != NULL) DeleteObject(font);
}

static const Module filewatch_module = {
    MOD, "1.2.0",
    filewatch_init, filewatch_menu, filewatch_command, filewatch_tick, filewatch_tip, filewatch_shutdown
};

TRAYCTL_PLUGIN(filewatch_module)
