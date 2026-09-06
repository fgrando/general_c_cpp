CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -g -O0 -Wall -Wextra

COMMON  = psuctrl_common.c psuctrl_common.h

TARGETS = psuctrl.exe psuctrl_status.exe

.PHONY: all release run clean

all: $(TARGETS)

# Tray app: GUI subsystem, needs shell32 (tray/ShellExecute) and gdi32 (icon).
psuctrl.exe: psuctrl.c $(COMMON)
	$(CC) $(CFLAGS) -mwindows -o $@ psuctrl.c psuctrl_common.c -lshell32 -lgdi32

# Status dump: console subsystem, no arguments.
psuctrl_status.exe: psuctrl_status.c $(COMMON)
	$(CC) $(CFLAGS) -o $@ psuctrl_status.c psuctrl_common.c

release: CFLAGS = -O2 -Wall -Wextra
release: all

# Launch the tray app in mock mode (no hardware / VISA runtime needed).
run: psuctrl.exe
	./psuctrl.exe --mock

clean:
	rm -f $(TARGETS)
