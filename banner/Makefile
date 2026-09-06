CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -g -O0 -Wall -Wextra -municode -mwindows
LDLIBS  = -lshell32 -lgdi32 -luser32
TARGET  = banner.exe
SRC     = banner.c

.PHONY: all release clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

release: CFLAGS = -O2 -Wall -Wextra -municode -mwindows
release: $(TARGET)

clean:
	rm -f $(TARGET)
