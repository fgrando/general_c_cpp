/* app.h - plugin ABI for TrayCtl. Every feature is a DLL, built from one
 * .c file, that exports trayctl_get_module() returning a single Module.
 * Drop a matching *.dll next to trayctl.exe to enable a feature; remove it
 * to disable one. The host never needs to be recompiled for either.
 */
#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>   /* NIIF_* */

/* ---- Host services, handed to each plugin at load time ----------------
 * A plugin never links against the host directly (there is no import lib
 * for trayctl.exe) - everything it needs comes through this table, valid
 * for the process lifetime. Store the pointer in init() if later callbacks
 * need it.
 */
typedef struct {
    HINSTANCE   inst;       /* for CreateWindowExA/RegisterClassA           */
    HWND        hwnd;       /* the host's hidden main window (dialog owner) */
    const char *ini_path;   /* trayctl.ini path, for direct GetPrivateProfile* calls */

    /* Balloon / toast notification. icon: NIIF_INFO, NIIF_WARNING, NIIF_ERROR */
    void (*notify)(const char *title, const char *text, DWORD icon);

    /* Tray icon is a red dot while any named source is on, green while all
     * are off. "source" is any string unique to your plugin (its module
     * name is a good choice) - no central registry or enum to edit. */
    void (*set_alert)(const char *source, int on);

    /* INI helpers (section/key in trayctl.ini). */
    int  (*ini_int)(const char *sec, const char *key, int def);
    void (*ini_str)(const char *sec, const char *key, const char *def,
                     char *out, DWORD size);
} HostAPI;

/* ---- Module interface -------------------------------------------------
 * Every callback may be NULL except init() - that's how you receive the
 * HostAPI, and even a plugin with nothing to configure needs it to reach
 * notify()/set_alert() later. The host refuses to load a plugin whose
 * init() is NULL rather than let it fault on a null host pointer.
 *
 * Menu IDs: the host gives each module a base ID and the module owns
 * base .. base+MODULE_ID_RANGE-1. Add items with base+n; command() gets n
 * back. Going past that range would dispatch into the next module, so a
 * plugin with many items (like tools.c) must cap its count.
 *
 * By convention across the bundled plugins:
 *   - offset 0 is reserved for display-only items (MF_GRAYED status lines).
 *     Grayed items can't be clicked, so their ID is never dispatched.
 *   - clickable items start at offset 1. Fixed items get a named enum;
 *     list-driven items use base+index+1.
 */
typedef struct {
    const char *name;                         /* also the duplicate-load key */
    const char *version;                      /* your own, e.g. "1.0.0"      */
    void (*init)(const HostAPI *host);        /* required: receives HostAPI  */
    void (*menu)(HMENU menu, UINT base);      /* append menu items           */
    void (*command)(UINT offset);             /* offset = id - base          */
    void (*tick)(void);                       /* called every 1 s            */
    void (*tip)(char *buf, int size);         /* optional tooltip line       */
    void (*shutdown)(void);                   /* cleanup on exit             */
} Module;

#define MODULE_ID_RANGE 100u

/* ---- ABI version --------------------------------------------------------
 * The host calls trayctl_abi_version() and compares major.minor BEFORE
 * calling trayctl_get_module() - a plugin built against a different ABI
 * must never have its Module struct read, because the struct's shape is
 * exactly what may have changed. That function's signature is therefore
 * permanently fixed, regardless of how everything else evolves.
 *
 * Which number to bump depends on WHO OWNS THE MEMORY:
 *
 *   HostAPI is allocated by the host and read by plugins, so appending a
 *   field is backward compatible - an older plugin simply never looks at
 *   the new tail. That's a MINOR bump.
 *
 *   Module is allocated by the PLUGIN and read by the host, so appending a
 *   field is NOT backward compatible: the host would read past the end of
 *   an older plugin's smaller struct and get garbage. ANY change to Module
 *   is a MAJOR bump.
 *
 * MAJOR also covers reordering or removing anything, or changing a
 * callback signature. PATCH is informational and never checked.
 */
#define TRAYCTL_ABI_MAJOR 2   /* 2.0.0: Module gained a version field */
#define TRAYCTL_ABI_MINOR 0
#define TRAYCTL_ABI_PATCH 0

typedef const Module *(*ModuleGetter)(void);
typedef void (*ModuleAbiVersionFn)(int *major, int *minor, int *patch);
#define TRAYCTL_MODULE_ENTRYPOINT "trayctl_get_module"
#define TRAYCTL_ABI_ENTRYPOINT    "trayctl_abi_version"

/* The whole export contract for a plugin. Put this once, at the bottom of
 * your .c file, naming your static Module:
 *     TRAYCTL_PLUGIN(myplugin_module)
 */
#define TRAYCTL_PLUGIN(module_symbol)                                         \
    __declspec(dllexport) const Module *trayctl_get_module(void)              \
    { return &(module_symbol); }                                              \
    __declspec(dllexport) void trayctl_abi_version(int *major, int *minor,    \
                                                   int *patch)                \
    { *major = TRAYCTL_ABI_MAJOR; *minor = TRAYCTL_ABI_MINOR;                 \
      *patch = TRAYCTL_ABI_PATCH; }

#endif
