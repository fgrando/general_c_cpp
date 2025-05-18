#include "backtrace.h"
#include <stdio.h>

/* the backtrace data */
static BACKTRACE_BUFFER_TYPE BACKTRACE_BUFFER[BACKTRACE_BUFFER_SIZE];

/* Buffer index*/
static int BACKTRACE_BUFFER_IDX = 0;

void BACKTRACE_PUSH(int id){
    if (BACKTRACE_BUFFER_IDX >= BACKTRACE_BUFFER_SIZE) {
        /* Buffer is full */
        return;
    }

    BACKTRACE_BUFFER[BACKTRACE_BUFFER_IDX] = id;
    printf("BACKTRACE_BUFFER[%d] = %d\n", BACKTRACE_BUFFER_IDX, id);

    if (BACKTRACE_BUFFER_CIRCULAR == 0) {
        BACKTRACE_BUFFER_IDX++;
    }
    else
    {
        /* Circular buffer */
        /* If the buffer is full, the oldest data will be overwritten*/
        BACKTRACE_BUFFER_IDX = (BACKTRACE_BUFFER_IDX+1) % BACKTRACE_BUFFER_SIZE;
    }
}
