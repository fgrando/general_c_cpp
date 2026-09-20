# TrayCtl

A tray-only Win32 utility: right-click the icon for the menu. `trayctl.exe`
itself does almost nothing — it owns the tray icon, builds the menu, shows
About, and loads plugins. Every actual feature is a DLL.

Drop a plugin DLL next to the exe to enable a feature; delete it to disable
one. Neither needs the host rebuilt, and a new plugin never needs the
host's source.

## Build

MinGW-w64, GNU make:

    mingw32-make CC=gcc                     # host + all plugins
    mingw32-make CC=x86_64-w64-mingw32-gcc  # cross-compile
    mingw32-make myplugin.dll CC=gcc        # just one plugin
    mingw32-make template.dll CC=gcc        # the reference plugin (see below)

`CC=gcc` is not optional in most setups: GNU make has a built-in default of
`CC=cc`, which pre-empts the makefile's `CC ?= gcc`, and MinGW ships no
`cc.exe`. Without it you get *"CreateProcess ... cc ... The system cannot
find the file specified"*.

## Deploying

Put these in one folder:

    trayctl.exe      the host
    trayctl.ini      configuration (optional; every key has a default)
    *.dll            one per feature you want

Current plugins:

| DLL             | Feature |
|-----------------|---------|
| `filewatch.dll` | Watch a path; show a docked banner while a file exists |
| `autologoff.dll`| Idle timer: log off / lock after N minutes with no input |
| `tools.dll`     | "Tools" submenu built from the ini |
| `launcher.dll`  | "Launcher" submenu of shortcuts fixed in code |
| `procterm.dll`  | "Terminate process…" by name or PID |

"About TrayCtl" and "Exit" are built into the host, not plugins — About
reports the host's own version and the ABI that plugins get checked
against, so it has to work even when no plugin loads at all. It also lists
every loaded plugin with its own version:

    TrayCtl
    Version 1.5.0
    Built Sep 19 2026 18:40:02

    Plugin ABI 2.0.0

    Plugins loaded (6):
        filewatch      1.2.0
        autologoff     2.1.0
        tools          1.1.0
        launcher       1.1.0
        procterm       1.1.0

That list is the quickest way to confirm a dropped-in plugin actually
loaded, and which build of it you're running.

Two caveats about that folder:

- **Every `*.dll` in it gets `LoadLibrary`'d**, which runs its `DllMain`
  before any version check can reject it. Only put DLLs you trust there.
- **Don't keep spare copies** like `filewatch_backup.dll` alongside the
  real one. The host now detects the duplicate by module name and skips it,
  but the tidy answer is to move retired plugins out of the folder.

## Architecture

    trayctl.exe ──┬── tray icon (one shared red/green dot)
                  ├── one right-click menu, assembled from all plugins
                  ├── one 1-second timer, ticking all plugins
                  └── HostAPI ──> filewatch.dll, … (each one .c file)

Everything runs on a **single UI thread, in one process**. That has two
consequences worth internalizing before writing a plugin:

- A plugin that blocks (slow I/O, a wedged serial port, a network share)
  stalls the tray icon *and every other plugin*. Do blocking work on your
  own thread — see "Threading" below.
- A plugin that crashes takes down the whole app. There is no sandbox: GCC
  doesn't support SEH (`__try`/`__except`), and real isolation would mean a
  process per plugin. The ABI check guards against *stale/incompatible*
  plugins, not buggy ones.

### Menu IDs

The host hands plugin *i* a base ID of `(i+1) * MODULE_ID_RANGE` (100) and
that plugin owns `base .. base+99`. You add items as `base+n`; `command(n)`
gets `n` back. Exceeding 99 items would dispatch into the next plugin's
range, so a plugin generating items from config (like `tools.c`) must cap
its count.

Conventions the bundled plugins share, worth following in a new one:

- **Offset 0 is display-only** — used for `MF_GRAYED` status lines. Grayed
  items can't be clicked, so their ID is never dispatched.
- **Clickable items start at offset 1.** Fixed items get a named `enum`
  (`CMD_REFRESH`, …) rather than bare `base + 2u`; list-driven items use
  `base + index + 1`.
- **One `#define MOD "name"` per plugin** supplies the `Module.name`, the
  ini section, and the `set_alert` key, so the module's identity is written
  once instead of three times.
- **Callbacks are prefixed with the full module name** (`filewatch_menu`,
  not `fw_menu`).

Load order follows directory enumeration, so menu section order can change
if you add or rename DLLs. IDs are rebuilt every time the menu opens, so
this is cosmetic only.

## Writing a plugin

**Start from `template.c`.** It is a complete, working reference plugin
that exercises every callback, with the reasoning written inline — copy it,
rename it, delete what you don't need:

    copy template.c myplugin.c
    # rename template_* and template_module, and set MOD to your module name
    mingw32-make myplugin.dll CC=gcc
    # drop myplugin.dll next to trayctl.exe and restart

`template.c` is intentionally **not** in `PLUGIN_SRCS`, so a normal build
doesn't drop a demo item into the real menu. Build it explicitly
(`mingw32-make template.dll CC=gcc`) if you want to see it run. Adding
*your* plugin to `PLUGIN_SRCS` is only needed to make it part of the
default build — the pattern rule builds any `.c` in the folder on demand.

The shape of it, in brief:

```c
#include "app.h"

#define MOD "mine"          /* Module.name, ini section, set_alert key */

enum { CMD_DO_THING = 1 };  /* offset 0 stays free for grayed items    */

static const HostAPI *host;

static void mine_init(const HostAPI *h) { host = h; }   /* required */

static void mine_menu(HMENU menu, UINT base)
{
    AppendMenuA(menu, MF_STRING, base + CMD_DO_THING, "Do the thing");
}

static void mine_command(UINT offset)
{
    if (offset == CMD_DO_THING) host->notify("Mine", "Did it", NIIF_INFO);
}

static const Module mine_module = {
    MOD,                    /* name: also the duplicate-load key  */
    "1.0.0",                /* your version, listed in About      */
    mine_init,              /* required                           */
    mine_menu, mine_command,
    NULL,                   /* tick     (every 1 s)               */
    NULL,                   /* tip      (tooltip line)            */
    NULL                    /* shutdown (cleanup on exit)         */
};

TRAYCTL_PLUGIN(mine_module)   /* exports both required entry points */
```

`version` is **your plugin's own** version string, free-form, and entirely
independent of the host version and the plugin ABI. It's what "About
TrayCtl" lists next to each loaded plugin, so use it to tell builds apart
at a glance.

Every callback is optional **except `init`** — that's the only way you
receive the `HostAPI`, so a plugin without it would fault the first time it
touched `host`. The host refuses to load such a plugin rather than let that
happen.

### Host services (`HostAPI`)

A plugin never links against `trayctl.exe` (there's no import library for
it); everything arrives through this table, valid for the process lifetime.

| Member | Purpose |
|--------|---------|
| `notify(title, text, icon)` | Balloon/toast. `NIIF_INFO`/`NIIF_WARNING`/`NIIF_ERROR`. |
| `set_alert(source, on)` | Tray dot is red while *any* source is on, green when all are off. |
| `ini_int(sec, key, def)` | Read an int from `trayctl.ini`. |
| `ini_str(sec, key, def, out, size)` | Read a string. |
| `inst` | `HINSTANCE` for `CreateWindowExA` / `RegisterClassA`. |
| `hwnd` | Host's hidden window — use as `MessageBoxA` owner. |
| `ini_path` | Raw ini path, for calls `ini_int`/`ini_str` don't cover (e.g. `GetPrivateProfileSectionA`, as `tools.c` uses to read a whole section). |

`set_alert` is keyed by an arbitrary **string**, not a shared enum, so a new
plugin can contribute a tray alert without editing any common file or
colliding with another plugin's flag. Use your module name.

### Threading

`init`, `menu`, `command`, `tick`, `tip` and `shutdown` all run on the UI
thread, and **`HostAPI` calls must stay on it**. If you need blocking work:

1. Do it on your own thread.
2. Cache the result behind a `CRITICAL_SECTION`.
3. Have `tick()` read the cache and call `set_alert`/`notify` from there.


## Plugin ABI versioning

`app.h` carries a three-integer version:

```c
#define TRAYCTL_ABI_MAJOR 1
#define TRAYCTL_ABI_MINOR 0
#define TRAYCTL_ABI_PATCH 0
```

`TRAYCTL_PLUGIN()` bakes those into the DLL via `trayctl_abi_version()`, a
function whose signature (three `int*` out-params) is **permanently frozen**
no matter how `HostAPI` or `Module` change. The loader calls it and compares
`major.minor` *before* it calls `trayctl_get_module()` or reads a single
field of the returned struct — because that struct's layout is precisely
what an ABI change would have moved. A mismatch is skipped with a balloon
saying which file and which version, and the app carries on.

When you change `app.h`, which number to bump depends on **who owns the
memory** — this asymmetry is easy to get wrong and the wrong call is a
crash, not a warning:

- **`HostAPI` is allocated by the host and read by plugins.** Appending a
  field is backward compatible: an older plugin simply never looks at the
  new tail. → **MINOR**, old plugins keep working.
- **`Module` is allocated by the plugin and read by the host.** Appending a
  field is *not* backward compatible: the host would read past the end of
  an older plugin's smaller struct and get garbage. → **ANY change to
  `Module` is MAJOR.**
- **MAJOR** also covers reordering or removing anything anywhere, and
  changing a callback signature. Every plugin must be rebuilt.
- **PATCH** — informational, never checked.

ABI 2.0.0 was exactly such a case: `Module` gained a `version` field, so
every plugin needed a rebuild and any 1.x DLL left in the folder is now
refused at load.

The About dialog shows the host's ABI version, which is what to compare
against when a plugin gets skipped.

## Configuration (`trayctl.ini`)

Lives next to the exe. Every key has a built-in default, so the file is
optional; a plugin whose section is absent just uses defaults. Plugins read
it at `init`, so changes need a restart — except `filewatch`, which has a
"Reload config" menu item.

```ini

[filewatch]
enabled=1
path=C:\temp\*.trigger      ; wildcards allowed
interval=2                  ; seconds between polls
message=Trigger file detected
banner_on_free=0            ; 1 = also show a green banner when absent

[autologoff]
enabled=1
minutes=10                  ; idle time allowed, in minutes (min 1)
action=logoff               ; logoff | lock  (use lock while testing)
force=0                     ; 0 = apps may prompt and cancel the logoff
                            ; 1 = force, UNSAVED WORK IS LOST
                            ; 2 = force only unresponsive apps

[tools]
; Name=path|arguments       ; path = exe, folder, document or URL
TeraTerm=C:\Program Files (x86)\teraterm\ttermpro.exe|/C=3 /BAUD=115200
Temp folder=C:\temp
```
