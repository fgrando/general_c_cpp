#ifndef WSA_ERROR_H
#define WSA_ERROR_H

#define WIN32_LEAN_AND_MEAN
#include <stdio.h>


#define PRINT_WSA_ERROR(...) do { \
							printf("[%s:%d] ", __FUNCTION__, __LINE__); \
							fprintf(stdout, __VA_ARGS__); \
							printf(" "); \
							wsa_error_print(); \
							printf("\n"); \
						}while(0)

void wsa_error_print();



#endif // !WSA_ERROR_H

