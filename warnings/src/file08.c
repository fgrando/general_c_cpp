#include <stdio.h>
int f08(void)
{
    printf("%d\n", "not an int");   /* -Wformat */
    return 0;
}
