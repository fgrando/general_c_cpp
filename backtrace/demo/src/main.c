#include "backtrace.h"
#include <stdio.h>
#include "calc.h"

int main(void) {
    // This is a simple C program that prints "Hello, World!" to the console.

/* BACKTRACE_INSERT_PROBE_HERE */
    printf("sum(1, 2) = %d\n", sum(1, 2));
/* BACKTRACE_INSERT_PROBE_HERE */
    printf("mult(1, 2) = %d\n", mult(1, 2));
/* BACKTRACE_INSERT_PROBE_HERE */
    printf("sub(1, 2) = %d\n", sub(1, 2));
/* BACKTRACE_INSERT_PROBE_HERE */
    printf("div(1, 2) = %d\n", div(1, 2));
/* BACKTRACE_INSERT_PROBE_HERE */
    printf("div(1, 2) = %d\n", div(0, 0));
    return 0;
}
