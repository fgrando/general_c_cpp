#ifndef BUILDTIMESTAMP_H
#define BUILDTIMESTAMP_H

#include <string.h>

// expand the macro with: g++ -E <file>

// length of build timestamp string
#define BUILD_TIMESTAMP_STR_LEN 16

// declares a timestamp string
#define BUILD_TIMESTAMP_DECL(var_name) char var_name[BUILD_TIMESTAMP_STR_LEN]; memset(var_name, 0x0, BUILD_TIMESTAMP_STR_LEN)

// get the timestamp and save to the given char pointer
#define BUILD_TIMESTAMP_GET(owner_string_ptr) \
do { \
    char *timestamp = owner_string_ptr;\
    /* Fill year */ \
    strncat(timestamp, &(__DATE__[7]),4); \
 \
    /* Fill month */ \
    const char *MONTH = &(__DATE__[0]); \
         if(strncmp(MONTH, "Jan", 3) == 0) strncat(timestamp, "01", 2); \
    else if(strncmp(MONTH, "Fev", 3) == 0) strncat(timestamp, "02", 2); \
    else if(strncmp(MONTH, "Mar", 3) == 0) strncat(timestamp, "03", 2); \
    else if(strncmp(MONTH, "Apr", 3) == 0) strncat(timestamp, "04", 2); \
    else if(strncmp(MONTH, "Mai", 3) == 0) strncat(timestamp, "05", 2); \
    else if(strncmp(MONTH, "Jun", 3) == 0) strncat(timestamp, "06", 2); \
    else if(strncmp(MONTH, "Jul", 3) == 0) strncat(timestamp, "07", 2); \
    else if(strncmp(MONTH, "Aug", 3) == 0) strncat(timestamp, "08", 2); \
    else if(strncmp(MONTH, "Sep", 3) == 0) strncat(timestamp, "09", 2); \
    else if(strncmp(MONTH, "Oct", 3) == 0) strncat(timestamp, "10", 2); \
    else if(strncmp(MONTH, "Nov", 3) == 0) strncat(timestamp, "11", 2); \
    else if(strncmp(MONTH, "Dez", 3) == 0) strncat(timestamp, "12", 2); \
    else                                   strncat(timestamp, "??", 2); \
 \
    /* Fill day */ \
    const char *DAY = &(__DATE__[4]); \
    if (DAY[0] == ' '){ \
        strncat(timestamp, "0", 1); \
        strncat(timestamp, &(DAY[1]),1); \
    } else { \
        strncat(timestamp, DAY,2); \
    } \
 \
    /* Fill time */ \
    strncat(timestamp, &(__TIME__[0]),2); \
    strncat(timestamp, &(__TIME__[3]),2); \
    strncat(timestamp, &(__TIME__[6]),2); \
} while(0)


// creates a new string with the given name and fill it with timestamp
#define BUILD_TIMESTAMP(new_variable_name)  \
    BUILD_TIMESTAMP_DECL(new_variable_name);\
    BUILD_TIMESTAMP_GET(new_variable_name)\


#endif

