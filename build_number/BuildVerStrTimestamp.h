#ifndef BUILD_VER_STR_TIMESTAMP_H
#define BUILD_VER_STR_TIMESTAMP_H

/**
 * Example:
 *  #include "BuildVerStrTimestamp.h"
 *  BUILD_VER_STR(version);
 *  cout << version << endl;
 *
**/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#include <string.h>

#define BUILD_VER_STR(PTR)                                                      \
    static char BUILD_VESION_FMT[20];                                           \
    const char* PTR = BUILD_VESION_FMT;                                         \
    do {                                                                        \
        memset(BUILD_VESION_FMT,0x0,sizeof(BUILD_VESION_FMT));                  \
                                                                                \
        static char MONTH[5];                                                   \
        memset(MONTH,0x0,sizeof(MONTH));                                        \
                                                                                \
        static char DAY[3];                                                     \
        memset(DAY,0x0,sizeof(DAY));                                            \
                                                                                \
        static char DATE_FMT[10];                                               \
        memset(DATE_FMT,0x0,sizeof(DATE_FMT));                                  \
                                                                                \
        /* YEAR */                                                              \
        int sz = 4;                                                             \
        strncat(DATE_FMT, &(__DATE__[7]),sz);                                   \
                                                                                \
        /* MONTH */                                                             \
        sz = 2;                                                                 \
        strncat(MONTH, &(__DATE__[0]),3);                                       \
        if(strncmp(MONTH, "Jan", 3) == 0)      strncat(DATE_FMT, "01", sz);     \
        else if(strncmp(MONTH, "Fev", 3) == 0) strncat(DATE_FMT, "02", sz);     \
        else if(strncmp(MONTH, "Mar", 3) == 0) strncat(DATE_FMT, "03", sz);     \
        else if(strncmp(MONTH, "Apr", 3) == 0) strncat(DATE_FMT, "04", sz);     \
        else if(strncmp(MONTH, "Mai", 3) == 0) strncat(DATE_FMT, "05", sz);     \
        else if(strncmp(MONTH, "Jun", 3) == 0) strncat(DATE_FMT, "06", sz);     \
        else if(strncmp(MONTH, "Jul", 3) == 0) strncat(DATE_FMT, "07", sz);     \
        else if(strncmp(MONTH, "Aug", 3) == 0) strncat(DATE_FMT, "08", sz);     \
        else if(strncmp(MONTH, "Sep", 3) == 0) strncat(DATE_FMT, "09", sz);     \
        else if(strncmp(MONTH, "Oct", 3) == 0) strncat(DATE_FMT, "10", sz);     \
        else if(strncmp(MONTH, "Nov", 3) == 0) strncat(DATE_FMT, "11", sz);     \
        else if(strncmp(MONTH, "Dez", 3) == 0) strncat(DATE_FMT, "12", sz);     \
        else strncat(DATE_FMT, "??", sz);                                       \
                                                                                \
        /* DAY */                                                               \
        strncat(DAY, &(__DATE__[4]),2);                                         \
        if (DAY[0] == ' '){                                                     \
            sz = 1;                                                             \
            strncat(DATE_FMT, "0", sz);                                         \
            strncat(DATE_FMT, &(DAY[1]),1);                                     \
        } else {                                                                \
            strncat(DATE_FMT, DAY,2);                                           \
        }                                                                       \
        strncat(BUILD_VESION_FMT, DATE_FMT, sizeof(BUILD_VESION_FMT));          \
                                                                                \
        /* TIME */                                                              \
        static char HHMMSS[7];                                                  \
        memset(HHMMSS,0x0,sizeof(HHMMSS));                                      \
        sz = 2;                                                                 \
        strncat(HHMMSS, &(__TIME__[0]),sz);                                     \
        strncat(HHMMSS, &(__TIME__[3]),sz);                                     \
        strncat(HHMMSS, &(__TIME__[6]),sz);                                     \
        strncat(BUILD_VESION_FMT, HHMMSS, sizeof(BUILD_VESION_FMT));            \
                                                                                \
        const char* PTR = BUILD_VESION_FMT;                                     \
    } while (0)




#ifdef __cplusplus
}
#endif // __cplusplus


#endif // BUILD_VER_STR_TIMESTAMP_H

