CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -g -O0 -Wall -Wextra
TARGET  = comlogger.exe
SRC     = comlogger.c

.PHONY: all release clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

release: CFLAGS = -O2 -Wall -Wextra
release: $(TARGET)

clean:
	rm -f $(TARGET)
