/* filewatch.c - poll a path pattern; while a matching file exists, show a
 * notification and dock a full-width banner to the top of the primary
 * monitor. Registered as an appbar (SHAppBarMessage) so maximized windows
 * are pushed down instead of covering it, and WS_EX_NOACTIVATE so it never
 * steals focus. Re-arms once the file is gone, with a second notification.
 * Behavior adapted from ../banner/banner.c.
 *
 * Wildcards are allowed in ANY component of the pattern, not just the file
 * name:
 *   C:\temp\*.trigger           file wildcard, one fixed folder
 *   C:\logs\run_*\*.trigger     '*' and '?' match within one folder level
 *   C:\Users\me\**\*.trigger    '**' matches that folder and its subtree,
 *                               down to max_depth levels
 * The shallowest match wins; ties within a folder go to whatever the file
 * system lists first.
 *
 * Because a '**' walk can take seconds, the search runs on its own thread
 * and the result is adopted by the next tick() - so every host-> call still
 * happens on the UI thread, as main.c requires.
 *
 * If the matched file's first line is non-empty it is shown alongside the
 * message, so whatever creates the file can leave a live detail string in
 * it (optional - an empty file works fine).
 */
#include "app.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Module identity: name in the Module struct, ini section, alert source. */
#define MOD "filewatch"
#define BANNER_H 34             /* logical px, scaled by DPI at runtime */
#define WM_APPBAR (WM_APP + 2)  /* private to the banner window */

/* A match found under '**' can be longer than MAX_PATH even when the
 * configured pattern is not, so matched paths get their own roomier buffer. */
#define FW_PATH      1024
/* Default '**' depth. Every extra level multiplies the work: under a whole
 * user profile a fruitless scan costs ~230 folders at depth 2, ~2.6k at 4,
 * ~18k at 6 and ~62k at 8 - and a fruitless scan is the steady state, since
 * the signal file is usually absent. 4 keeps that under half a second. */
#define FW_DEPTH_DEF 4
#define FW_DEPTH_MAX 64
/* Runaway guard: depth bounds how DEEP a walk goes but not how WIDE, so a
 * pathologically broad tree still needs a ceiling. Tripping it is reported
 * in the status line rather than passed off as "nothing found". */
#define FW_MAX_DIRS  200000

/* Menu item IDs, as offsets from the base the host hands us. */
enum {
    CMD_TOGGLE_WATCH = 1,
    CMD_REFRESH,
    CMD_RELOAD,
    CMD_TOGGLE_BANNER,
    CMD_REVEAL
};

static const HostAPI *host;
static char path[MAX_PATH], message[128], found[FW_PATH];
static char status[160], detail[200];
static int  enabled, interval, counter, triggered, started;
static int  max_depth;
static int  banner_on_free, banner_enabled = 1;
static HWND banner;
static HFONT font;
static int  banner_h = BANNER_H;
static int  appbar_registered;

/* ---- scan thread ------------------------------------------------------
 * Only the worker touches scan_pattern/scan_depth (snapshots, so a Reload
 * mid-scan can't change the pattern under it), scanned/scan_capped, and
 * the scan_result* buffers. It publishes them by setting scan_ready last;
 * InterlockedExchange is a full barrier, so a tick() that sees the flag
 * sees finished buffers.
 */
static volatile LONG scan_busy;     /* a worker is running                  */
static volatile LONG scan_ready;    /* a result is waiting to be adopted    */
static volatile LONG scan_abort;    /* set at shutdown: stop walking        */
static char scan_pattern[MAX_PATH];
static int  scan_depth;
static char scan_result[FW_PATH];
static char scan_result_detail[200];
static int  scan_result_capped;
static long scanned;                /* folders visited by the current scan  */
static int  scan_capped;

/* ------------------------------------------------------- pattern matching */

/* Case-insensitive glob over ONE path component: '*' matches any run of
 * characters, '?' exactly one. Used for folder names only - the file name
 * component is still handed to FindFirstFileA, so its matching stays
 * exactly what existing configs already rely on. */
static int match_component(const char *pat, const char *name)
{
    while (*pat != '\0') {
        if (*pat == '*') {
            pat++;
            if (*pat == '\0') return 1;      /* trailing '*' takes the rest */
            while (*name != '\0') {
                if (match_component(pat, name)) return 1;
                name++;
            }
            return 0;
        }
        if (*name == '\0') return 0;
        if (*pat != '?' &&
            tolower((unsigned char)*pat) != tolower((unsigned char)*name)) {
            return 0;
        }
        pat++;
        name++;
    }
    return *name == '\0';
}

static int has_wild(const char *s)
{
    return strpbrk(s, "*?") != NULL;
}

static int search_dir(const char *dir, const char *pat,
                      char *out, size_t outsz, int budget);

/* Last pattern component: FindFirstFileA does the name matching. */
static int match_file(const char *dir, const char *pat,
                      char *out, size_t outsz)
{
    char glob[FW_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE f;
    int hit = 0;

    snprintf(glob, sizeof(glob), "%s%s", dir, pat);
    f = FindFirstFileA(glob, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        snprintf(out, outsz, "%s%s", dir, fd.cFileName);
        hit = 1;
    } while (!hit && FindNextFileA(f, &fd));
    FindClose(f);
    return hit;
}

/* Recurse into every subfolder of dir whose name matches dirpat, looking
 * for tail below it. Stops at the first match. */
static int descend(const char *dir, const char *dirpat, const char *tail,
                   char *out, size_t outsz, int budget)
{
    char glob[FW_PATH], sub[FW_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE f;
    int hit = 0;

    snprintf(glob, sizeof(glob), "%s*", dir);
    f = FindFirstFileA(glob, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        /* Reparse points are skipped: a junction pointing at its own parent
         * would make the walk never end. */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (strcmp(fd.cFileName, ".") == 0) continue;
        if (strcmp(fd.cFileName, "..") == 0) continue;
        if (!match_component(dirpat, fd.cFileName)) continue;
        if (scan_abort) break;
        if (++scanned > FW_MAX_DIRS) { scan_capped = 1; break; }
        snprintf(sub, sizeof(sub), "%s%s\\", dir, fd.cFileName);
        hit = search_dir(sub, tail, out, outsz, budget);
    } while (!hit && FindNextFileA(f, &fd));
    FindClose(f);
    return hit;
}

/* Match the backslash-separated pattern tail `pat` under the literal
 * folder `dir` - which ends in a backslash, or is empty for a pattern
 * relative to the current directory. `budget` is how many folder levels a
 * '**' is still allowed to expand to; components spelled out in the
 * pattern don't spend it, so a fixed-depth pattern works at budget 0. */
static int search_dir(const char *dir, const char *pat,
                      char *out, size_t outsz, int budget)
{
    char comp[MAX_PATH], sub[FW_PATH];
    const char *slash, *rest;
    size_t len;

    if (scan_abort) return 0;

    slash = strchr(pat, '\\');
    if (slash == NULL) return match_file(dir, pat, out, outsz);

    len = (size_t)(slash - pat);
    if (len >= sizeof(comp)) return 0;
    memcpy(comp, pat, len);
    comp[len] = '\0';
    rest = slash + 1;
    while (*rest == '\\') rest++;       /* tolerate doubled separators */
    if (*rest == '\0') return 0;        /* pattern ends in a separator */

    if (strcmp(comp, "**") == 0) {
        /* Zero folders: try the tail right here. */
        if (search_dir(dir, rest, out, outsz, budget)) return 1;
        /* One more folder, then '**' again - spends one level of budget. */
        if (budget <= 0) return 0;
        return descend(dir, "*", pat, out, outsz, budget - 1);
    }
    if (strcmp(comp, ".") == 0) return search_dir(dir, rest, out, outsz, budget);
    if (has_wild(comp)) return descend(dir, comp, rest, out, outsz, budget);

    /* Literal folder: no enumeration needed, just append it. */
    snprintf(sub, sizeof(sub), "%s%s\\", dir, comp);
    return search_dir(sub, rest, out, outsz, budget);
}

/* Full path of the first file matching `pattern`, or 0 if there is none.
 * Everything up to the last separator before the first wildcard is literal,
 * so the walk starts there instead of at the drive root.
 *
 * A '**' is matched by iterative deepening - zero extra levels, then one,
 * then two - so the SHALLOWEST match wins and a file dropped right under
 * the root is found without first descending into some unrelated deep
 * subtree. Re-walking the shallow levels is cheap next to the deepest one.
 */
static int find_match(const char *pattern, int depth, char *out, size_t outsz)
{
    const char *cut = strpbrk(pattern, "*?");
    char base[FW_PATH];
    size_t len;
    int budget;

    scanned = 0;
    scan_capped = 0;

    if (cut == NULL) {
        /* No wildcard anywhere: a plain existence check, as before. */
        DWORD attr = GetFileAttributesA(pattern);
        if (attr == INVALID_FILE_ATTRIBUTES) return 0;
        if (attr & FILE_ATTRIBUTE_DIRECTORY) return 0;
        snprintf(out, outsz, "%s", pattern);
        return 1;
    }

    while (cut > pattern && cut[-1] != '\\') cut--;
    len = (size_t)(cut - pattern);
    if (len >= sizeof(base)) return 0;
    memcpy(base, pattern, len);
    base[len] = '\0';

    if (strstr(cut, "**") == NULL) {
        return search_dir(base, cut, out, outsz, 0);
    }
    for (budget = 0; budget <= depth; budget++) {
        if (search_dir(base, cut, out, outsz, budget)) return 1;
        if (scan_capped || scan_abort) break;
    }
    return 0;
}

/* ---------------------------------------------------------------- helpers */

/* First line of the matched file, if any - whatever was written there. */
static void read_detail(const char *full_path, char *out, size_t outsz)
{
    HANDLE h;
    char buf[200];
    DWORD got = 0;
    size_t i;

    out[0] = '\0';
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
        lstrcpynA(out, buf, (int)outsz);
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
    char *p;

    enabled  = host->ini_int(MOD, "enabled", 1);
    interval = host->ini_int(MOD, "interval", 2);
    if (interval < 1) interval = 1;
    max_depth = host->ini_int(MOD, "max_depth", FW_DEPTH_DEF);
    if (max_depth < 0) max_depth = 0;
    if (max_depth > FW_DEPTH_MAX) max_depth = FW_DEPTH_MAX;
    host->ini_str(MOD, "path", "C:\\temp\\*.trigger", path, sizeof(path));
    host->ini_str(MOD, "message", "File detected", message, sizeof(message));
    banner_on_free = host->ini_int(MOD, "banner_on_free", 0);

    /* The matcher splits on backslashes only, so accept forward slashes in
     * the ini rather than silently treating them as part of a name. */
    for (p = path; *p != '\0'; p++) {
        if (*p == '/') *p = '\\';
    }
}

/* Runs on the worker thread: nothing here may touch host-> or the UI. */
static DWORD WINAPI scan_proc(LPVOID unused)
{
    (void)unused;

    scan_result[0] = '\0';
    scan_result_detail[0] = '\0';
    if (find_match(scan_pattern, scan_depth, scan_result, sizeof(scan_result))) {
        read_detail(scan_result, scan_result_detail, sizeof(scan_result_detail));
    } else {
        scan_result[0] = '\0';
    }
    scan_result_capped = scan_capped;

    InterlockedExchange(&scan_ready, 1);   /* publishes everything above */
    InterlockedExchange(&scan_busy, 0);
    return 0;
}

/* Kick off a scan unless one is already running. A '**' pattern can take
 * longer than `interval`; the extra kicks are simply dropped. */
static void scan_start(void)
{
    HANDLE t;

    if (InterlockedCompareExchange(&scan_busy, 1, 0) != 0) return;

    lstrcpynA(scan_pattern, path, sizeof(scan_pattern));
    scan_depth = max_depth;
    if (!started) lstrcpynA(status, "Scanning...", sizeof(status));

    t = CreateThread(NULL, 0, scan_proc, NULL, 0, NULL);
    if (t == NULL) {
        InterlockedExchange(&scan_busy, 0);
        return;
    }
    CloseHandle(t);   /* fire and forget: the result comes back via scan_ready */
}

/* Adopt a finished scan. UI thread only - host->set_alert and host->notify
 * must not be called from the worker (see main.c). */
static void scan_adopt(void)
{
    int was = triggered;

    triggered = scan_result[0] != '\0';
    if (triggered) {
        lstrcpynA(found, scan_result, sizeof(found));
        lstrcpynA(detail, scan_result_detail, sizeof(detail));
        lstrcpynA(status, message, sizeof(status));
    } else {
        found[0] = '\0';
        detail[0] = '\0';
        lstrcpynA(status, scan_result_capped
                  ? "Too many folders to search - narrow the path"
                  : "Signal file not present", sizeof(status));
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
    /* Clear the flag either way; a result that arrives while the watch is
     * off is dropped rather than allowed to resurrect the alert. */
    if (InterlockedCompareExchange(&scan_ready, 0, 1) == 1 && enabled) {
        scan_adopt();
    }
    if (!enabled || ++counter < interval) return;
    counter = 0;
    scan_start();
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
    /* With wildcards in the folder part the match can be anywhere under the
     * pattern, so show which file it actually was. */
    if (triggered && found[0]) {
        AppendMenuA(menu, MF_STRING | MF_GRAYED, base, found);
    }
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
            scan_start();
        } else {
            triggered = 0;
            detail[0] = '\0';
            found[0] = '\0';
            host->set_alert(MOD, 0);
            banner_show(0);
        }
        break;
    case CMD_REFRESH:
        counter = 0;
        scan_start();
        break;
    case CMD_RELOAD:
        filewatch_load_config();
        counter = 0;
        started = 0;
        scan_start();
        host->notify("File watch", "Configuration reloaded.", NIIF_INFO);
        break;
    case CMD_TOGGLE_BANNER:
        banner_enabled = !banner_enabled;
        banner_show(triggered || banner_on_free);
        break;
    case CMD_REVEAL: {
        char param[FW_PATH + 16];
        if (!found[0]) break;
        snprintf(param, sizeof(param), "/select,\"%s\"", found);
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
    /* Tell a running walk to stop, but don't wait on it: main.c never
     * unloads plugin DLLs precisely so a thread can outlive shutdown. */
    InterlockedExchange(&scan_abort, 1);
    appbar_remove();
    if (banner != NULL) DestroyWindow(banner);
    if (font != NULL) DeleteObject(font);
}

static const Module filewatch_module = {
    MOD, "1.3.0",
    filewatch_init, filewatch_menu, filewatch_command, filewatch_tick, filewatch_tip, filewatch_shutdown
};

TRAYCTL_PLUGIN(filewatch_module)
