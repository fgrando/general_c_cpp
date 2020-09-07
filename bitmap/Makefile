CC=gcc
CFLAGS=-I.

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

bitmap: main.o
	$(CC) -o bitmap main.o
	rm *.o

clean:
	rm bitmap
