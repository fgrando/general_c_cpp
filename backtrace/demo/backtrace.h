#ifndef BACKTRACE_ADBE02AC_0BA6_4683_99E9_D04FDAB7F07C
#define BACKTRACE_ADBE02AC_0BA6_4683_99E9_D04FDAB7F07C

/* The comment "BACKTRACE_INSERT_PROBE_HERE" is replaced by  BACKTRACE_PUSH() calls in source code files */

/* Flag to indicate if the buffer is circular or not.
 * Circular buffer will allow data to be overwritten. */
#define BACKTRACE_BUFFER_CIRCULAR  0

/* Type of the buffer. uint8 is enough up to 255 probes, otherwise go to uint16 or uint32 */
#define BACKTRACE_BUFFER_TYPE char

/* Number of traces stored */
#define BACKTRACE_BUFFER_SIZE 30

void BACKTRACE_PUSH(int);

#endif /* BACKTRACE_ADBE02AC_0BA6_4683_99E9_D04FDAB7F07C */
