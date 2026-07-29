#include "common.h"
int f01(void)
{
    int unused_a;          /* -Wunused-variable */
    int set_b = 3;         /* -Wunused-but-set-variable */
    set_b = 4;
    return 0;
}
