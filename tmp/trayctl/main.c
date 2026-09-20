/* main.c - TrayCtl host: tray icon, right-click menu, and a small plugin
 * loader. Every *.dll next to the exe that exports trayctl_get_module()
 * (see app.h) is loaded as a module at startup - drop one in to add a
 * feature, delete it to remove one, no recompile of the host needed.
 */
#include "app.h"
#include <string.h>
#include <stdio.h>

#define TRAYCTL_VERSION "1.5.0"

#define MAX_MODULES  32
#define WM_TRAY      (WM_APP + 1)
#define TIMER_TICK   1u
/* The host's own menu IDs. Module IDs start at MODULE_ID_RANGE, so
 * anything below that is free for the host. */
enum { ID_EXIT = 1, ID_ABOUT };

/* Host-private: plugins get these through HostAPI, never by linking. */
static HINSTANCE g_inst;
static HWND      g_hwnd;
static char      g_ini[MAX_PATH];

static const Module *modules[MAX_MODULES];
static UINT num_modules;

static NOTIFYICONDATAA nid;
static UINT wm_taskbar_created;
static int  alert_on;
static HICON icon_green, icon_red;

/* Alert sources are named by the plugin, not a compile-time enum, so a
 * plugin dropped in later can contribute one without the host (or any
 * other plugin) ever being touched. */
typedef struct { char name[64]; int on; } AlertEntry;
static AlertEntry alert_table[MAX_MODULES];
static UINT alert_count;

#define DOT_SIZE 16

/* Filled circle with alpha transparency, used for the red/green tray dot. */
static HICON make_dot_icon(COLORREF fill)
{
    BITMAPV5HEADER bi;
    HDC dc;
    void *bits = NULL;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON icon;
    DWORD *px;
    int x, y, cx, cy, r;

    memset(&bi, 0, sizeof(bi));
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = DOT_SIZE;
    bi.bV5Height      = DOT_SIZE;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    dc = GetDC(NULL);
    color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (color == NULL || bits == NULL) return NULL;

    px = (DWORD *)bits;
    cx = cy = DOT_SIZE / 2;
    r = DOT_SIZE / 2 - 1;
    for (y = 0; y < DOT_SIZE; y++) {
        for (x = 0; x < DOT_SIZE; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r) {
                px[y * DOT_SIZE + x] = 0xFF000000u
                    | ((DWORD)GetRValue(fill) << 16)
                    | ((DWORD)GetGValue(fill) << 8)
                    | (DWORD)GetBValue(fill);
            } else {
                px[y * DOT_SIZE + x] = 0;              /* transparent */
            }
        }
    }

    mask = CreateBitmap(DOT_SIZE, DOT_SIZE, 1, 1, NULL); /* ignored: color bitmap has alpha */

    memset(&ii, 0, sizeof(ii));
    ii.fIcon    = TRUE;
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

/* ---- Host services, exposed to plugins via HostAPI --------------------- */
static void host_notify(const char *title, const char *text, DWORD icon)
{
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = icon;
    lstrcpynA(nid.szInfoTitle, title, sizeof(nid.szInfoTitle));
    lstrcpynA(nid.szInfo, text, sizeof(nid.szInfo));
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

/* Only ever called from the UI thread - a plugin doing background work
 * (e.g. a poll thread) must hand its result to a tick/command callback
 * first, which then calls this from the UI thread like everyone else. */
static void host_set_alert(const char *source, int on)
{
    UINT i;
    int any;

    for (i = 0; i < alert_count; i++) {
        if (lstrcmpiA(alert_table[i].name, source) == 0) break;
    }
    if (i == alert_count) {
        if (alert_count >= MAX_MODULES) return;
        lstrcpynA(alert_table[i].name, source, sizeof(alert_table[i].name));
        alert_count++;
    }
    alert_table[i].on = on ? 1 : 0;

    any = 0;
    for (i = 0; i < alert_count; i++) any |= alert_table[i].on;
    if (any == alert_on) return;

    alert_on = any;
    nid.uFlags = NIF_ICON;
    nid.hIcon = alert_on ? icon_red : icon_green;
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

static int host_ini_int(const char *sec, const char *key, int def)
{
    return (int)GetPrivateProfileIntA(sec, key, def, g_ini);
}

static void host_ini_str(const char *sec, const char *key, const char *def,
                          char *out, DWORD size)
{
    GetPrivateProfileStringA(sec, key, def, out, size, g_ini);
}

static HostAPI host_api;

/* ---- Plugin loading ----------------------------------------------------
 * Loaded DLLs are intentionally never FreeLibrary'd once accepted: a plugin
 * may still have a background thread running (e.g. a poll thread) when the
 * app exits, and unmapping its code while that thread is executing inside
 * it is undefined behavior. Letting ExitProcess tear the whole thing down
 * at once avoids that; the OS reclaims the handles either way. */

/* GetProcAddress hands back a FARPROC, which by design has a different
 * signature than whatever we're fetching; casting it straight across trips
 * -Wcast-function-type, so pun through a union. */
#define GET_PROC(dll, name, type, out) \
    do { union { FARPROC p; type f; } u_; \
         u_.p = GetProcAddress((dll), (name)); (out) = u_.f; } while (0)

static int name_already_loaded(const char *name)
{
    UINT i;
    for (i = 0; i < num_modules; i++) {
        if (lstrcmpiA(modules[i]->name, name) == 0) return 1;
    }
    return 0;
}

/* Returns the plugin's Module, or NULL if the DLL isn't a usable plugin.
 * Rejected DLLs are unloaded; accepted ones stay mapped for good. */
static const Module *load_one(const char *path, const char *filename)
{
    HMODULE dll;
    ModuleAbiVersionFn abi_ver;
    ModuleGetter get;
    const Module *m;
    int major = -1, minor = -1, patch = -1;
    char msg[MAX_PATH + 96];

    dll = LoadLibraryA(path);
    if (dll == NULL) return NULL;

    /* Version check FIRST, via the permanently fixed-shape function - only
     * once it passes is it safe to read the plugin's Module struct, whose
     * layout is exactly what an ABI mismatch may have changed. */
    GET_PROC(dll, TRAYCTL_ABI_ENTRYPOINT, ModuleAbiVersionFn, abi_ver);
    if (abi_ver == NULL) { FreeLibrary(dll); return NULL; }  /* not a plugin */

    abi_ver(&major, &minor, &patch);
    if (major != TRAYCTL_ABI_MAJOR || minor != TRAYCTL_ABI_MINOR) {
        snprintf(msg, sizeof(msg),
                 "%s: built for plugin ABI %d.%d.%d, this host is %d.%d.%d - skipped.",
                 filename, major, minor, patch,
                 TRAYCTL_ABI_MAJOR, TRAYCTL_ABI_MINOR, TRAYCTL_ABI_PATCH);
        host_notify("Plugin skipped", msg, NIIF_WARNING);
        FreeLibrary(dll);
        return NULL;
    }

    GET_PROC(dll, TRAYCTL_MODULE_ENTRYPOINT, ModuleGetter, get);
    if (get == NULL) { FreeLibrary(dll); return NULL; }

    m = get();
    /* init() is what delivers the HostAPI; without it the plugin would
     * fault the moment it tried to notify() or set_alert(). */
    if (m == NULL || m->name == NULL || m->init == NULL) {
        snprintf(msg, sizeof(msg), "%s: not a well-formed plugin - skipped.", filename);
        host_notify("Plugin skipped", msg, NIIF_WARNING);
        FreeLibrary(dll);
        return NULL;
    }

    /* A second copy of the same plugin (e.g. a "filewatch_backup.dll" left
     * in the folder) would register duplicate menu entries and fight over
     * the same window classes. */
    if (name_already_loaded(m->name)) {
        snprintf(msg, sizeof(msg), "%s: '%s' is already loaded - skipped.",
                 filename, m->name);
        host_notify("Plugin skipped", msg, NIIF_WARNING);
        FreeLibrary(dll);
        return NULL;
    }
    return m;
}

static void load_modules(void)
{
    char dir[MAX_PATH], pattern[MAX_PATH + 8], full[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE f;
    char *slash;

    lstrcpynA(dir, g_ini, sizeof(dir));
    slash = strrchr(dir, '\\');
    if (slash != NULL) slash[1] = '\0';
    snprintf(pattern, sizeof(pattern), "%s*.dll", dir);

    f = FindFirstFileA(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        const Module *m;

        if (num_modules >= MAX_MODULES) break;
        snprintf(full, sizeof(full), "%s%s", dir, fd.cFileName);

        m = load_one(full, fd.cFileName);
        if (m != NULL) modules[num_modules++] = m;
    } while (FindNextFileA(f, &fd));
    FindClose(f);
}

/* ---- Tray icon ------------------------------------------------------- */
static void tray_add(void)
{
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = alert_on ? icon_red : icon_green;
    lstrcpynA(nid.szTip, "TrayCtl", sizeof(nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &nid);
}

/* Rebuilt every tick, but only pushed to the shell when it actually
 * changed - otherwise this is an IPC round-trip once a second forever. */
static void tray_update_tip(void)
{
    char tip[128] = "TrayCtl";
    char line[64];
    UINT i;

    for (i = 0; i < num_modules; i++) {
        if (modules[i]->tip != NULL) {
            line[0] = '\0';
            modules[i]->tip(line, sizeof(line));
            if (line[0] != '\0' &&
                strlen(tip) + strlen(line) + 2 < sizeof(tip)) {
                strcat(tip, "\n");
                strcat(tip, line);
            }
        }
    }
    if (strcmp(tip, nid.szTip) == 0) return;

    nid.uFlags = NIF_TIP;
    lstrcpynA(nid.szTip, tip, sizeof(nid.szTip));
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

/* Host-owned, not a plugin: it reports the host's own version and the ABI
 * that plugins are checked against, so it has to be there even when no
 * plugin is. */
static void show_about(void)
{
    /* Room for the header plus one line per plugin. name/version are
     * plugin-supplied pointers, so each line is bounded with a precision
     * rather than trusted to be short. */
    char text[MAX_MODULES * 96 + 256];
    int n;
    UINT i;

    n = snprintf(text, sizeof(text),
                 "TrayCtl\nVersion %s\nBuilt %s %s\n\n"
                 "Plugin ABI %d.%d.%d\n\n",
                 TRAYCTL_VERSION, __DATE__, __TIME__,
                 TRAYCTL_ABI_MAJOR, TRAYCTL_ABI_MINOR, TRAYCTL_ABI_PATCH);

    if (num_modules == 0) {
        snprintf(text + n, sizeof(text) - (size_t)n,
                 "No plugins loaded.\n(No compatible *.dll next to the exe.)");
    } else {
        n += snprintf(text + n, sizeof(text) - (size_t)n,
                      "Plugins loaded (%u):\n", num_modules);
        for (i = 0; i < num_modules && n < (int)sizeof(text); i++) {
            const char *ver = modules[i]->version;
            n += snprintf(text + n, sizeof(text) - (size_t)n,
                          "    %-14.32s %.24s\n",
                          modules[i]->name, (ver != NULL) ? ver : "?");
        }
    }
    MessageBoxA(g_hwnd, text, "About TrayCtl", MB_OK | MB_ICONINFORMATION);
}

static void show_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    UINT i, cmd;

    for (i = 0; i < num_modules; i++) {
        if (modules[i]->menu != NULL) {
            int before = GetMenuItemCount(menu);
            modules[i]->menu(menu, (i + 1u) * MODULE_ID_RANGE);
            if (GetMenuItemCount(menu) > before) {
                AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
            }
        }
    }
    AppendMenuA(menu, MF_STRING, ID_ABOUT, "About TrayCtl");
    AppendMenuA(menu, MF_STRING, ID_EXIT, "Exit");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);           /* so the menu closes properly */
    cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == ID_EXIT) {
        DestroyWindow(g_hwnd);
    } else if (cmd == ID_ABOUT) {
        show_about();
    } else if (cmd >= MODULE_ID_RANGE) {
        i = cmd / MODULE_ID_RANGE - 1u;
        if (i < num_modules && modules[i]->command != NULL) {
            modules[i]->command(cmd % MODULE_ID_RANGE);
        }
    }
}

/* ---- Window procedure ------------------------------------------------ */
static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    UINT i;

    if (msg == wm_taskbar_created && msg != 0) {   /* explorer restarted */
        tray_add();
        return 0;
    }
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) {
            show_menu();
        }
        return 0;
    case WM_TIMER:
        if (wp != TIMER_TICK) break;
        for (i = 0; i < num_modules; i++) {
            if (modules[i]->tick != NULL) modules[i]->tick();
        }
        tray_update_tip();
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_TICK);
        for (i = 0; i < num_modules; i++) {
            if (modules[i]->shutdown != NULL) modules[i]->shutdown();
        }
        Shell_NotifyIconA(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSA wc;
    MSG msg;
    char *p;
    UINT i;

    (void)prev; (void)cmdline; (void)show;
    g_inst = inst;

    /* single instance */
    CreateMutexA(NULL, TRUE, "TrayCtl_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    /* ini = <exe dir>\trayctl.ini; plugins are loaded from the same dir */
    GetModuleFileNameA(NULL, g_ini, MAX_PATH);
    p = strrchr(g_ini, '\\');
    if (p != NULL) p[1] = '\0';
    strncat(g_ini, "trayctl.ini", MAX_PATH - strlen(g_ini) - 1);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = "TrayCtlWnd";
    RegisterClassA(&wc);

    /* hidden top-level window (not HWND_MESSAGE: needs TaskbarCreated) */
    g_hwnd = CreateWindowA("TrayCtlWnd", "TrayCtl", 0, 0, 0, 0, 0,
                           NULL, NULL, inst, NULL);
    wm_taskbar_created = RegisterWindowMessageA("TaskbarCreated");

    icon_green = make_dot_icon(RGB(40, 170, 60));
    icon_red   = make_dot_icon(RGB(220, 40, 40));
    if (icon_green == NULL) icon_green = LoadIcon(NULL, IDI_APPLICATION);
    if (icon_red == NULL)   icon_red   = LoadIcon(NULL, IDI_WARNING);

    host_api.inst      = g_inst;
    host_api.hwnd      = g_hwnd;
    host_api.ini_path  = g_ini;
    host_api.notify    = host_notify;
    host_api.set_alert = host_set_alert;
    host_api.ini_int   = host_ini_int;
    host_api.ini_str   = host_ini_str;

    tray_add();             /* before load_modules(): the loader notifies */
    load_modules();
    for (i = 0; i < num_modules; i++) {
        modules[i]->init(&host_api);   /* load_one() guarantees non-NULL */
    }
    SetTimer(g_hwnd, TIMER_TICK, 1000, NULL);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
