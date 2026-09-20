/* autologoff.c - idle timer: logs the user off (or locks) once the machine
 * has gone untouched for longer than the allowance in the ini.
 *
 * Idle time is read from the system (GetLastInputInfo) rather than counted
 * up here, so it is the real thing: any mouse or keyboard activity puts it
 * straight back to zero, and our own tick cadence can't make it drift.
 *
 * Menu: "Auto logoff when idle 00:10:00 (idle now 00:00:04)" - checked =
 *       enabled, click toggles - plus a grayed line with the projected
 *       logoff time and how much is left.
 *
 * Note the idle counter always reads near zero whenever you are looking at
 * it: opening the menu or moving the mouse to the tray icon IS input, so it
 * resets what you came to read. It climbs while you hold still.
 */
#include "app.h"
#include <stdio.h>
#include <time.h>

/* Module identity: name in the Module struct, ini section, alert source. */
#define MOD "autologoff"

/* Menu IDs as offsets from our base; 0 is the grayed detail line. */
enum { CMD_TOGGLE = 1 };

/* [autologoff] force= : how hard to push when apps object to the logoff.
 * An app with unsaved work can answer WM_QUERYENDSESSION with a prompt and
 * cancel the logoff outright, which is useless for an unattended timer -
 * but overriding it throws that unsaved work away. */
enum {
    FORCE_NEVER   = 0,   /* apps may prompt and cancel (default, safe)       */
    FORCE_ALWAYS  = 1,   /* EWX_FORCE: no prompts, UNSAVED WORK IS LOST      */
    FORCE_IF_HUNG = 2    /* EWX_FORCEIFHUNG: only override unresponsive apps */
};

/* Warn when this much of the allowance is left, in seconds, descending.
 * Thresholds that don't fit inside the allowance are skipped. */
static const int warn_at[] = { 600, 300, 60 };
#define NUM_WARN ((int)(sizeof(warn_at) / sizeof(warn_at[0])))

static const HostAPI *host;
static int   enabled;       /* feature on/off (menu toggles it)              */
static int   allowed;       /* idle seconds permitted before firing          */
static int   idle;          /* idle seconds right now, from the system       */
static int   warn_window;   /* tray dot goes red with this much left         */
static int   warned;        /* bitmask of warn_at[] entries already notified */
static int   force;         /* one of the FORCE_* values                     */
static char  action[16];    /* "logoff" or "lock" (lock is the safe test)    */

/* Seconds since the last input anywhere in the session. The DWORD
 * subtraction is deliberate: both values wrap about every 49 days, and
 * unsigned arithmetic gives the right answer straight through the wrap. */
static int system_idle(void)
{
    LASTINPUTINFO lii;

    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) return 0;
    return (int)((GetTickCount() - lii.dwTime) / 1000u);
}

static void fmt_hms(char *buf, int size, int secs)
{
    if (secs < 0) secs = 0;
    snprintf(buf, (size_t)size, "%02d:%02d:%02d",
             secs / 3600, (secs / 60) % 60, secs % 60);
}

/* Wall-clock time it would fire, assuming no input from now on. */
static void fmt_deadline(char *buf, int size)
{
    time_t target = time(NULL) + (allowed - idle);
    struct tm *t = localtime(&target);
    snprintf(buf, (size_t)size, "%02d:%02d:%02d",
             t->tm_hour, t->tm_min, t->tm_sec);
}

static void fire(void)
{
    if (lstrcmpiA(action, "lock") == 0) {
        LockWorkStation();
    } else {
        /* Plain EWX_LOGOFF sends WM_QUERYENDSESSION and lets any app with
         * unsaved work prompt and cancel the whole thing - which defeats an
         * unattended timer. See the `force` key in the ini. */
        UINT flags = EWX_LOGOFF;
        if (force == FORCE_ALWAYS)       flags |= EWX_FORCE;
        else if (force == FORCE_IF_HUNG) flags |= EWX_FORCEIFHUNG;
        ExitWindowsEx(flags, SHTDN_REASON_FLAG_PLANNED);
    }
}

/* ---------------------------------------------------------------- module */

static void autologoff_init(const HostAPI *h)
{
    int i;

    host    = h;
    enabled = host->ini_int(MOD, "enabled", 1);
    allowed = host->ini_int(MOD, "minutes", 10) * 60;
    if (allowed < 60) allowed = 60;
    force   = host->ini_int(MOD, "force", FORCE_NEVER);
    host->ini_str(MOD, "action", "logoff", action, sizeof(action));

    /* Red from the largest warning that actually fits in the allowance, so
     * a 10-minute limit doesn't start out already red. */
    warn_window = 0;
    for (i = 0; i < NUM_WARN; i++) {
        if (warn_at[i] < allowed) { warn_window = warn_at[i]; break; }
    }
    if (warn_window == 0) warn_window = allowed / 2;

    idle = system_idle();
}

static void autologoff_tick(void)
{
    int was = idle, left, i;

    if (!enabled) {
        host->set_alert(MOD, 0);
        return;
    }

    idle = system_idle();
    if (idle < was) warned = 0;      /* input happened: re-arm the warnings */
    left = allowed - idle;

    if (left <= 0) {
        enabled = 0;                  /* one shot; re-enable from the menu */
        warned = 0;
        host->set_alert(MOD, 0);
        fire();
        return;
    }

    for (i = 0; i < NUM_WARN; i++) {
        if (warn_at[i] >= allowed) continue;          /* doesn't fit */
        if (left > warn_at[i] || (warned & (1 << i))) continue;
        warned |= 1 << i;
        {
            char deadline[16], left_s[16], idle_s[16], t[160];
            fmt_deadline(deadline, sizeof(deadline));
            fmt_hms(left_s, sizeof(left_s), left);
            fmt_hms(idle_s, sizeof(idle_s), idle);
            snprintf(t, sizeof(t), "Idle %s - %s at %s, %s left. "
                     "Move the mouse to reset.",
                     idle_s, action, deadline, left_s);
            host->notify("Auto logoff", t, NIIF_WARNING);
        }
    }

    host->set_alert(MOD, left <= warn_window);
}

static void autologoff_menu(HMENU menu, UINT base)
{
    char text[160], idle_s[16], allow_s[16], left_s[16], deadline[16];

    idle = system_idle();
    fmt_hms(allow_s, sizeof(allow_s), allowed);

    if (enabled) {
        fmt_hms(idle_s, sizeof(idle_s), idle);
        snprintf(text, sizeof(text), "Auto %s when idle %s   (idle now %s)",
                 action, allow_s, idle_s);
    } else {
        snprintf(text, sizeof(text), "Auto %s (inactive, idle limit %s)",
                 action, allow_s);
    }
    AppendMenuA(menu, MF_STRING | (enabled ? MF_CHECKED : 0),
                base + CMD_TOGGLE, text);

    if (enabled) {
        fmt_deadline(deadline, sizeof(deadline));
        fmt_hms(left_s, sizeof(left_s), allowed - idle);
        snprintf(text, sizeof(text), "        %s at %s  (%s left if idle)",
                 action, deadline, left_s);
        AppendMenuA(menu, MF_STRING | MF_GRAYED, base, text);
    }
}

static void autologoff_command(UINT off)
{
    if (off != CMD_TOGGLE) return;
    enabled = !enabled;
    warned = 0;
    idle = system_idle();
    host->set_alert(MOD, 0);
}

static void autologoff_tip(char *buf, int size)
{
    char idle_s[16], left_s[16];

    if (!enabled) return;
    fmt_hms(idle_s, sizeof(idle_s), idle);
    fmt_hms(left_s, sizeof(left_s), allowed - idle);
    snprintf(buf, (size_t)size, "idle %s, %s in %s", idle_s, action, left_s);
}

static const Module autologoff_module = {
    MOD, "2.1.0",
    autologoff_init, autologoff_menu, autologoff_command,
    autologoff_tick, autologoff_tip, NULL
};

TRAYCTL_PLUGIN(autologoff_module)
