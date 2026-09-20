/* psu.c - MOCK power supply control: 3 independent output channels, each
 * with an alias name read from the ini.
 *
 * Menu: channel 1 sits at the top level as a shortcut, and a "PSU" submenu
 * holds all three, so two rarely-touched channels don't crowd the main
 * menu. Both copies of channel 1 use the same command ID.
 *
 * There is no real instrument link yet - toggling a channel just flips an
 * in-memory flag and notifies. Once
 * the VISA/SCPI wiring exists, replace psu_set_channel()'s body with the
 * real channel-select + OUTP ON|OFF write (and, if you add a read-back,
 * keep channels[idx].on in sync with what the instrument actually
 * reports); the menu, aliases, tray alert and tooltip all stay the same.
 *
 * NOTE for that future change: VISA/serial I/O blocks. Earlier in this
 * project's history psu.c talked to a real COM port synchronously and had
 * to be moved onto its own polling thread so a slow/wedged port couldn't
 * stall the tray icon and every other plugin (they all share one UI
 * thread). Re-apply that pattern once real I/O comes back: do the
 * blocking call off the UI thread, cache the result behind a lock, and
 * only call host->set_alert()/host->notify() from tick()/command() (UI
 * thread), never from the background thread itself.
 */
#include "app.h"
#include <stdio.h>

/* Module identity: name in the Module struct, ini section, alert source. */
#define MOD "psu"

#define NUM_CHANNELS 3
#define CH_NAME_LEN  32

typedef struct {
    char name[CH_NAME_LEN];  /* alias from ini, e.g. "5V Rail" */
    int  on;                 /* mocked state */
} Channel;

static const HostAPI *host;
static int enabled;
static Channel channels[NUM_CHANNELS];

static int any_channel_on(void)
{
    int i;
    for (i = 0; i < NUM_CHANNELS; i++) if (channels[i].on) return 1;
    return 0;
}

/* MOCK: replace this body with the real VISA write once the instrument
 * link exists - see the file header. */
static void psu_set_channel(int idx, int on)
{
    char msg[64];

    channels[idx].on = on ? 1 : 0;
    host->set_alert(MOD, any_channel_on());
    snprintf(msg, sizeof(msg), "%s: %s", channels[idx].name, on ? "ON" : "OFF");
    host->notify("PSU", msg, NIIF_INFO);
}

static void psu_init(const HostAPI *h)
{
    char key[16], def[16];
    int i;

    host = h;
    enabled = host->ini_int(MOD, "enabled", 1);
    for (i = 0; i < NUM_CHANNELS; i++) {
        snprintf(key, sizeof(key), "ch%d_name", i + 1);
        snprintf(def, sizeof(def), "Channel %d", i + 1);
        host->ini_str(MOD, key, def, channels[i].name, sizeof(channels[i].name));
        channels[i].on = 0;   /* mock: always starts off */
    }
}

/* "<prefix><alias>: ON  - click to turn OFF" */
static void fmt_channel(char *buf, int size, const char *prefix, int idx)
{
    snprintf(buf, (size_t)size, "%s%s: %s  - click to turn %s",
             prefix, channels[idx].name, channels[idx].on ? "ON" : "OFF",
             channels[idx].on ? "OFF" : "ON");
}

static void psu_menu(HMENU menu, UINT base)
{
    HMENU sub;
    char text[160];
    int i;

    if (!enabled) return;

    /* Channel 1 is promoted to the top level as a shortcut - it's the one
     * reached for most often. */
    fmt_channel(text, sizeof(text), "PSU ", 0);
    AppendMenuA(menu, MF_STRING | (channels[0].on ? MF_CHECKED : 0),
                base + 1u, text);

    /* Everything, including channel 1 again, lives under a "PSU" submenu so
     * the main menu stays short. Both entries carry the same command ID, so
     * either one toggles the same channel. */
    sub = CreatePopupMenu();
    for (i = 0; i < NUM_CHANNELS; i++) {
        fmt_channel(text, sizeof(text), "", i);   /* submenu is already titled */
        AppendMenuA(sub, MF_STRING | (channels[i].on ? MF_CHECKED : 0),
                    base + (UINT)i + 1u, text);
    }
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)sub, "PSU");
}

static void psu_command(UINT off)
{
    int idx = (int)off - 1;
    if (idx < 0 || idx >= NUM_CHANNELS) return;
    psu_set_channel(idx, !channels[idx].on);
}

/* The host's tooltip line is short, so summarize rather than listing three
 * aliases that would just get truncated. */
static void psu_tip(char *buf, int size)
{
    int i, on = 0;

    if (!enabled) return;
    for (i = 0; i < NUM_CHANNELS; i++) on += channels[i].on;
    snprintf(buf, (size_t)size, "PSU: %d of %d channels on", on, NUM_CHANNELS);
}

static const Module psu_module = {
    MOD, "0.9.2-mock",
    psu_init, psu_menu, psu_command, NULL, psu_tip, NULL
};

TRAYCTL_PLUGIN(psu_module)
