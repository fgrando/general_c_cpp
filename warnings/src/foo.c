#include "bad.h"
int main(void) {
    unsigned u = 1;
    if (u > -1) return 0;          /* -Wsign-compare */
    return undeclared_func();       /* -Wimplicit-function-declaration */
}