/* launcher.c - "Launcher" submenu of built-in Windows admin shortcuts.
 * Unlike tools.c (user-configurable via the ini), these entries are fixed
 * in code. Add more rows to `items[]` below to extend the menu.
 */
#include "app.h"

/* Module identity: name in the Module struct (no ini section). */
#define MOD "launcher"

typedef struct {
    const char *name;
    const char *path;      /* exe, document, or URL (ShellExecute) */
    const char *args;      /* NULL if none */
} LauncherItem;

static const LauncherItem items[] = {
    { "Device Manager", "devmgmt.msc", NULL },
};
#define NUM_ITEMS ((int)(sizeof(items) / sizeof(items[0])))

static const HostAPI *host;

static void launcher_init(const HostAPI *h)
{
    host = h;
}

static void launcher_menu(HMENU menu, UINT base)
{
    HMENU sub;
    int i;

    sub = CreatePopupMenu();
    for (i = 0; i < NUM_ITEMS; i++) {
        AppendMenuA(sub, MF_STRING, base + (UINT)i + 1u, items[i].name);
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)sub, "Launcher");
}

static void launcher_command(UINT off)
{
    int i = (int)off - 1;          /* offsets are 1-based; 0 is display-only */
    HINSTANCE r;

    if (i < 0 || i >= NUM_ITEMS) return;
    r = ShellExecuteA(NULL, "open", items[i].path, items[i].args,
                      NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        host->notify("Launcher", items[i].path, NIIF_ERROR);
    }
}

static const Module launcher_module = {
    MOD, "1.1.0",
    launcher_init, launcher_menu, launcher_command, NULL, NULL, NULL
};

TRAYCTL_PLUGIN(launcher_module)
