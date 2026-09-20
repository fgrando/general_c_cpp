/* procterm.c - "Terminate process..." - type a name (e.g. "notepad" or
 * "notepad.exe") or a PID, confirm, and it's killed, like Task Manager's
 * "End task". Matches by name can hit more than one process (e.g. several
 * chrome.exe); all matches are listed in the confirmation and terminated
 * together.
 *
 * There's no dialog resource / resource compiler in this project's build,
 * so the input prompt is a small plain window with EDIT/BUTTON children,
 * the same raw-Win32 style filewatch.c uses for its banner. It's pumped by
 * main.c's shared GetMessage loop like every other top-level window here.
 */
#include "app.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Module identity: name in the Module struct (no ini section of its own). */
#define MOD "procterm"

/* Menu ID as an offset from our base. */
enum { CMD_PROMPT = 1 };

/* Child-control IDs for the prompt window - a separate namespace from the
 * menu offsets above. */
enum { ID_EDIT = 101, ID_OK, ID_CANCEL };

#define PROMPT_W   320
#define PROMPT_H   110
#define MAX_MATCHES 64
#define NAME_LEN    64     /* image names are short; MAX_PATH here would be 16 KB of stack */

typedef struct { DWORD pid; char name[NAME_LEN]; } Match;

static const HostAPI *host;
static HWND prompt_wnd, edit_wnd;
static WNDPROC orig_edit_proc;

/* ---------------------------------------------------------- process lookup */

static int is_all_digits(const char *s)
{
    if (*s == '\0') return 0;
    while (*s) { if (!isdigit((unsigned char)*s)) return 0; s++; }
    return 1;
}

static int find_by_name(const char *query, Match *out, int cap)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    char qname[MAX_PATH];
    int count = 0;
    size_t n;

    lstrcpynA(qname, query, sizeof(qname));
    n = strlen(qname);
    if (n < 4 || _stricmp(qname + n - 4, ".exe") != 0) lstrcatA(qname, ".exe");

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (count >= cap) break;
            if (_stricmp(pe.szExeFile, qname) == 0) {
                out[count].pid = pe.th32ProcessID;
                lstrcpynA(out[count].name, pe.szExeFile, sizeof(out[count].name));
                count++;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return count;
}

static int find_by_pid(DWORD pid, Match *out)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    int found = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                out->pid = pid;
                lstrcpynA(out->name, pe.szExeFile, sizeof(out->name));
                found = 1;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static void terminate_matches(const Match *m, int count)
{
    char list[1024] = "", q[1200], summary[160];
    DWORD self = GetCurrentProcessId();
    int i, killed = 0, failed = 0, skipped = 0;

    for (i = 0; i < count; i++) {
        char line[64];
        snprintf(line, sizeof(line), "%s  (PID %lu)\r\n", m[i].name, (unsigned long)m[i].pid);
        if (strlen(list) + strlen(line) < sizeof(list)) strcat(list, line);
    }

    snprintf(q, sizeof(q), "Terminate the following process(es)?\r\n\r\n%s", list);
    if (MessageBoxA(prompt_wnd, q, "Terminate Process", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    for (i = 0; i < count; i++) {
        HANDLE h;
        if (m[i].pid == self) { skipped++; continue; }
        h = OpenProcess(PROCESS_TERMINATE, FALSE, m[i].pid);
        if (h == NULL) { failed++; continue; }
        if (TerminateProcess(h, 1)) killed++; else failed++;
        CloseHandle(h);
    }

    snprintf(summary, sizeof(summary), "%d terminated, %d failed%s.",
             killed, failed, skipped ? ", 1 skipped (TrayCtl itself)" : "");
    host->notify("Terminate process", summary, failed ? NIIF_WARNING : NIIF_INFO);
}

static void do_terminate(const char *query)
{
    Match matches[MAX_MATCHES];
    int count;

    if (query[0] == '\0') return;

    if (is_all_digits(query)) {
        Match m;
        DWORD pid = (DWORD)strtoul(query, NULL, 10);
        if (!find_by_pid(pid, &m)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "No process with PID %lu.", (unsigned long)pid);
            MessageBoxA(prompt_wnd, msg, "Terminate Process", MB_OK | MB_ICONWARNING);
            return;
        }
        terminate_matches(&m, 1);
        return;
    }

    count = find_by_name(query, matches, MAX_MATCHES);
    if (count == 0) {
        char msg[MAX_PATH + 40];
        snprintf(msg, sizeof(msg), "No process named '%s'.", query);
        MessageBoxA(prompt_wnd, msg, "Terminate Process", MB_OK | MB_ICONWARNING);
        return;
    }
    terminate_matches(matches, count);
}

/* ---------------------------------------------------------------- prompt */

/* Enter submits, Escape cancels - a plain EDIT control doesn't do either
 * on its own without a real dialog loop, so subclass it. */
static LRESULT CALLBACK edit_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { PostMessageA(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_OK, 0), 0); return 0; }
        if (wp == VK_ESCAPE) { PostMessageA(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_CANCEL, 0), 0); return 0; }
    }
    return CallWindowProcA(orig_edit_proc, h, msg, wp, lp);
}

static LRESULT CALLBACK prompt_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OK: {
            char text[MAX_PATH];
            char *s;
            size_t n;

            GetWindowTextA(edit_wnd, text, sizeof(text));
            s = text;
            while (*s == ' ') s++;
            n = strlen(s);
            while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
            do_terminate(s);
            DestroyWindow(h);
            return 0;
        }
        case ID_CANCEL:
            DestroyWindow(h);
            return 0;
        default:
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        prompt_wnd = NULL;
        edit_wnd = NULL;
        return 0;
    default:
        break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void show_prompt(void)
{
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - PROMPT_W) / 2;
    int y = (sh - PROMPT_H) / 2;
    HFONT f;
    HWND c;

    if (prompt_wnd != NULL) {
        SetForegroundWindow(prompt_wnd);
        return;
    }

    prompt_wnd = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        "TrayCtlProcTerm", "Terminate Process",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, PROMPT_W, PROMPT_H, NULL, NULL, host->inst, NULL);
    if (prompt_wnd == NULL) return;

    CreateWindowExA(0, "STATIC", "Process name or PID:",
                    WS_CHILD | WS_VISIBLE, 12, 12, PROMPT_W - 24, 16,
                    prompt_wnd, NULL, host->inst, NULL);

    edit_wnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    12, 32, PROMPT_W - 24, 22,
                    prompt_wnd, (HMENU)(UINT_PTR)ID_EDIT, host->inst, NULL);

    CreateWindowExA(0, "BUTTON", "Terminate",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    PROMPT_W - 176, 66, 80, 24,
                    prompt_wnd, (HMENU)(UINT_PTR)ID_OK, host->inst, NULL);

    CreateWindowExA(0, "BUTTON", "Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    PROMPT_W - 88, 66, 80, 24,
                    prompt_wnd, (HMENU)(UINT_PTR)ID_CANCEL, host->inst, NULL);

    f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (c = GetWindow(prompt_wnd, GW_CHILD); c != NULL; c = GetWindow(c, GW_HWNDNEXT)) {
        SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    }

    orig_edit_proc = (WNDPROC)SetWindowLongPtrA(edit_wnd, GWLP_WNDPROC, (LONG_PTR)edit_subclass);

    ShowWindow(prompt_wnd, SW_SHOW);
    SetForegroundWindow(prompt_wnd);
    SetFocus(edit_wnd);
}

/* ---------------------------------------------------------------- module */

static void procterm_init(const HostAPI *h)
{
    WNDCLASSA wc;

    host = h;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = prompt_proc;
    wc.hInstance = host->inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TrayCtlProcTerm";
    RegisterClassA(&wc);
}

static void procterm_menu(HMENU menu, UINT base)
{
    AppendMenuA(menu, MF_STRING, base + CMD_PROMPT, "Terminate process...");
}

static void procterm_command(UINT off)
{
    if (off != CMD_PROMPT) return;
    show_prompt();
}

static const Module procterm_module = {
    MOD, "1.1.0",
    procterm_init, procterm_menu, procterm_command, NULL, NULL, NULL
};

TRAYCTL_PLUGIN(procterm_module)
