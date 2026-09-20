# TrayCtl - GNU Make + MinGW-w64
# Native:  mingw32-make
# Cross:   make CC=x86_64-w64-mingw32-gcc
#
# trayctl.exe is just the host: tray icon, menu, About, and a plugin
# loader. Every feature lives in its own plugin.dll, built from one .c file
# against app.h. Drop a plugin next to the exe to enable it, delete it to
# disable it - the host never needs to be rebuilt either way.
# Start a new plugin by copying template.c.
CC       ?= gcc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600
LDFLAGS  = -mwindows -static
DLLFLAGS = -shared -static
LIBS     = -lshell32 -luser32 -lgdi32

# template.c is the reference plugin and is deliberately NOT listed here,
# so a normal build doesn't drop a demo item into the real menu. Build it
# on demand with:  mingw32-make template.dll
PLUGIN_SRCS = psu.c filewatch.c autologoff.c tools.c launcher.c procterm.c
PLUGINS     = $(PLUGIN_SRCS:.c=.dll)

all: trayctl.exe $(PLUGINS)

trayctl.exe: main.o
	$(CC) $(LDFLAGS) -o $@ main.o $(LIBS)

main.o: main.c app.h
	$(CC) $(CFLAGS) -c main.c

%.dll: %.c app.h
	$(CC) $(CFLAGS) $(DLLFLAGS) -o $@ $< $(LIBS)

clean:
	-del /q *.o *.dll trayctl.exe 2>nul || rm -f *.o *.dll trayctl.exe

.PHONY: all clean
