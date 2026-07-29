#include "common.h"
int f02(unsigned n)
{
    int i;
    int total = 0;
    for (i = 0; i < n; i++) {   /* -Wsign-compare */
        total += i;
    }
    if (n >= 0) {               /* -Wtype-limits: unsigned >= 0 always true */
        total++;
    }
    return total;
}
