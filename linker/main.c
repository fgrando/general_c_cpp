#include "sw_header.h"
#include <stdio.h>
#include <stdint.h>

extern sw_header_t gl_header;

extern char _fernando[];
extern char _fernando2[];
extern char _fernando3[];

int main() {
    printf("hello world\n");
    printf("version %04X CRC %08x\n", gl_header.version, gl_header.crc);



    printf("ptr %p\n", (void*)_fernando);
    printf("ptr %p\n", (void*)_fernando2);
    printf("ptr %p\n", (void*)_fernando3);


    return 0;
}

#include <stdint.h>
