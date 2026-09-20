/* tools.c - "Tools" submenu built from the [tools] section of the ini.
 * Format:  Name=path|arguments        (arguments optional)
 * path can be an exe, a folder, a document or a URL (ShellExecute).
 */
#include "app.h"
#include <string.h>

/* Module identity: name in the Module struct and ini section. */
#define MOD "tools"

#define MAX_TOOLS 32

static const HostAPI *host;
static char section[4096];
static const char *names[MAX_TOOLS], *paths[MAX_TOOLS], *args[MAX_TOOLS];
static int count;

static void tools_init(const HostAPI *h)
{
    char *p, *next, *eq, *bar;

    host = h;
    count = 0;
    GetPrivateProfileSectionA(MOD, section, sizeof(section), host->ini_path);

    /* section = "name=path|args\0name=path\0\0" -> split in place */
    for (p = section; *p != '\0' && count < MAX_TOOLS; p = next) {
        next = p + strlen(p) + 1;          /* before we insert '\0's */
        eq = strchr(p, '=');
        if (eq == NULL || *p == ';') continue;
        *eq = '\0';
        bar = strchr(eq + 1, '|');
        if (bar != NULL) *bar = '\0';
        names[count] = p;
        paths[count] = eq + 1;
        args[count]  = (bar != NULL) ? bar + 1 : NULL;
        count++;
    }
}

static void tools_menu(HMENU menu, UINT base)
{
    HMENU sub;
    int i;

    if (count == 0) return;
    sub = CreatePopupMenu();
    for (i = 0; i < count; i++) {
        AppendMenuA(sub, MF_STRING, base + (UINT)i + 1u, names[i]);
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)sub, "Tools");
}

static void tools_command(UINT off)
{
    int i = (int)off - 1;          /* offsets are 1-based; 0 is display-only */
    HINSTANCE r;

    if (i < 0 || i >= count) return;
    r = ShellExecuteA(NULL, "open", paths[i], args[i], NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        host->notify("Tools", paths[i], NIIF_ERROR);
    }
}

static const Module tools_module = {
    MOD, "1.1.0",
    tools_init, tools_menu, tools_command, NULL, NULL, NULL
};

TRAYCTL_PLUGIN(tools_module)
