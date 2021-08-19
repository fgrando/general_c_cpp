#include <stdio.h>
#include "getendian.h"

int main(){
    int big;

    GET_ENDIAN(big);

    big = GET_BIGENDIAN;

    if (big)
    {
        printf("I am big endian");
    }
    else
    {
        printf("I am little endian");
    }

    return 0;
}
