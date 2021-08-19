#ifndef GET_ENDIAN_H_INCLUDED_
#define GET_ENDIAN_H_INCLUDED_

/*
 * The value 0x0A0B0C0D storage in memory depends on the computer endianness:
 * Memory:   x1 x2 x3 x4
 * Big...:   0A 0B 0C 0D
 * Little:   0D 0C 0B 0A
 *
 * Similarly, storing the value 1 in the union below:
 * Memory:    x1   x2   x3   x4
 * char[4]   c[0] c[1] c[2] c[3]
 * Big...:    0    0    0    1
 * Little:    1    0    0    0
 *
*/
#define GET_ENDIAN(isBigEndian) do { \
    union UMem {unsigned int i; char c[sizeof(unsigned int)];} mem; \
    mem.i = 1U; isBigEndian = (mem.c[0] == 0) ? 1 : 0; \
} while(0)

#endif
