#include "backtrace.h"

int mult(int a, int b) {
/* BACKTRACE_INSERT_PROBE_HERE */
    return a * b;
}

int sub(int a, int b) {
/* BACKTRACE_INSERT_PROBE_HERE */
    return a - b;
}