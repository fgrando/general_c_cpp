#ifndef DEBUG_H

#ifndef NO_DEBUG

#ifdef __cplusplus
#include <cstdio>
#else
#include <stdio.h>
#endif

/* Prints function name, line, the user message and a new line */
#define DEBUG_PRINT(...) do {printf("[%s:%d] ", __FUNCTION__, __LINE__); fprintf(stdout, __VA_ARGS__); printf("\n"); }while(0)

#else
#define DEBUG_PRINT(...) do {}while(0)
#endif
#endif