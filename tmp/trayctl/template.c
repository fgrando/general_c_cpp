/* template.c - reference plugin. Copy this file, rename it, and you have a
 * working feature; every callback below is exercised so you can delete what
 * you don't need rather than look up what you do.
 *
 *     mingw32-make template.dll CC=gcc     build it
 *     copy template.dll next to trayctl.exe, restart TrayCtl
 *
 * It is deliberately NOT in the Makefile's PLUGIN_SRCS, so a normal build
 * doesn't drop a demo item into the real menu.
 *
 * What it does: a "Demo active" toggle that drives the tray dot, a "Say
 * hello" item that raises a notification, a counter ticking once a second,
 * and a tooltip line. All of it configurable from [template] in the ini.
 *
 * The three rules worth knowing before you write a real one:
 *
 *   1. init() is the only required callback. It hands you the HostAPI,
 *      which is your ONLY connection to the host - plugins do not link
 *      against trayctl.exe. Stash the pointer; it stays valid for the
 *      process lifetime. A plugin without init() is refused at load.
 *
 *   2. Everything here runs on the host's single UI thread, shared with
 *      every other plugin. Blocking in any callback freezes the tray icon
 *      and all other features. If you need slow I/O, do it on your own
 *      thread, cache the result behind a CRITICAL_SECTION, and let tick()
 *      read the cache and make the HostAPI calls - never call host->
 *      anything from your own thread.
 *
 *   3. You own menu IDs base+1 .. base+99 (MODULE_ID_RANGE-1). command()
 *      receives the offset back. Going past that range dispatches into the
 *      next plugin, so cap anything generated from config. Offset 0 is by
 *      convention a display-only (grayed) item.
 */
#include "app.h"
#include <stdio.h>

/* Module identity, in one place: the name in the Module struct below, the
 * ini section we read, and the key we pass to host->set_alert(). The
 * bundled plugins all follow this pattern. */
#define MOD "template"

/* Menu IDs as offsets from the base the host gives us. Naming them beats
 * scattering base+1u / base+2u across menu() and command(). Offset 0 is
 * reserved by convention for display-only (MF_GRAYED) items, which are
 * never dispatched because they can't be clicked. */
enum {
    CMD_TOGGLE = 1,
    CMD_HELLO
};

static const HostAPI *host;     /* rule 1: stashed in init(), used everywhere */
static int  enabled;            /* [template] enabled=1                       */
static int  interval;           /* [template] interval=5   (tick throttle)    */
static char greeting[64];       /* [template] greeting=...                    */
static int  active;             /* runtime state the menu toggles             */
static int  counter, ticks;

/* ---------------------------------------------------------------- module */

/* Required. Read config, register window classes, start threads. */
static void template_init(const HostAPI *h)
{
    host = h;

    enabled  = host->ini_int(MOD, "enabled", 1);
    interval = host->ini_int(MOD, "interval", 5);
    if (interval < 1) interval = 1;
    host->ini_str(MOD, "greeting", "Hello from the template plugin",
                  greeting, sizeof(greeting));
}

/* Append your items. Called fresh every time the user opens the menu, so
 * just read your current state - no need to cache menu handles. */
static void template_menu(HMENU menu, UINT base)
{
    char text[96];

    if (!enabled) return;

    AppendMenuA(menu, MF_STRING | (active ? MF_CHECKED : 0),
                base + CMD_TOGGLE, "Demo active");

    /* MF_GRAYED items can't be clicked, so their ID is never dispatched -
     * handy for showing status. */
    snprintf(text, sizeof(text), "Ticked %d time(s)", ticks);
    AppendMenuA(menu, MF_STRING | MF_GRAYED, base, text);

    AppendMenuA(menu, MF_STRING, base + CMD_HELLO, "Say hello");
}

/* offset == the n you passed as base+n in menu(). */
static void template_command(UINT offset)
{
    switch (offset) {
    case CMD_TOGGLE:
        active = !active;
        /* The tray dot is red while ANY plugin's source is on. The string
         * is just a key - use your module name and you can't collide with
         * another plugin. */
        host->set_alert(MOD, active);
        break;
    case CMD_HELLO:
        host->notify("Template", greeting, NIIF_INFO);
        break;
    default:
        break;
    }
}

/* Called once a second. Keep it cheap - see rule 2. The counter pattern
 * below is how to run at your own slower interval. */
static void template_tick(void)
{
    if (!enabled || !active) return;
    if (++counter < interval) return;
    counter = 0;
    ticks++;
}

/* One short line for the tray tooltip. The host's buffer is small, so
 * summarize rather than dumping detail that would just be truncated. */
static void template_tip(char *buf, int size)
{
    if (!enabled || !active) return;
    snprintf(buf, (size_t)size, "Template: %d tick(s)", ticks);
}

/* Called on exit, before the process goes away. Destroy windows and free
 * GDI objects here. Don't block: if you started a thread, signal it and
 * return rather than waiting on something that may be wedged. */
static void template_shutdown(void)
{
    if (active) host->set_alert(MOD, 0);
}

static const Module template_module = {
    MOD,                /* name: also the duplicate-load key            */
    "1.0.0",            /* your own version, listed in About - not the ABI */
    template_init,      /* required                                     */
    template_menu,      /* NULL if you add no menu items                */
    template_command,   /* NULL if you have no clickable items          */
    template_tick,      /* NULL if you don't need the 1 s timer         */
    template_tip,       /* NULL if you don't want a tooltip line        */
    template_shutdown   /* NULL if you have nothing to clean up         */
};

/* Exports both entry points the loader needs: the module getter and the
 * frozen-signature ABI stamp it version-checks first. */
TRAYCTL_PLUGIN(template_module)
