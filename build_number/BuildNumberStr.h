#ifndef BUILD_NUMBER_STR_H

// expand the macro with: g++ -E BuildNumberStr.h

extern "C"{

/**
Scrip example to generate GlobalRevNumber.txt

    #!/bin/bash
    revnumber=$(svnversion ${PWD})
    echo \"$revnumber\" > GlobalRevNumber.txt

**/


#define MYBUILD_NUMBER_ 0
#define MYBUILD_NUMBER_TIMESTAMP 0
#define MYBUILD_NUMBER_SVN_REV 0
#define MYBUILD_NUMBER_TIMESTAMP_SVN_REV 1



#if (MYBUILD_NUMBER_)
#define BUILD_NUMBER_STR() BUILD_NUMBER
    const char* BUILD_NUMBER =
    __DATE__
    __TIME__
    ;


#elif (MYBUILD_NUMBER_TIMESTAMP)
#define BUILD_NUMBER_STR() BUILD_NUMBER()
    #include <string.h>
    const char* DATE() {
        static char MONTH[5];
        memset(MONTH,0x0,sizeof(MONTH));

        static char DAY[3];
        memset(DAY,0x0,sizeof(DAY));

        static char DATE_FMT[10];
        memset(DATE_FMT,0x0,sizeof(DATE_FMT));

        // YEAR
        strncat(DATE_FMT, &(__DATE__[7]),4);

        // MONTH
        strncat(MONTH, &(__DATE__[0]),3);
        if(strncmp(MONTH, "Jan", 3) == 0) strncat(DATE_FMT, "01", 2);
        else if(strncmp(MONTH, "Fev", 3) == 0) strncat(DATE_FMT, "02", 2);
        else if(strncmp(MONTH, "Mar", 3) == 0) strncat(DATE_FMT, "03", 2);
        else if(strncmp(MONTH, "Apr", 3) == 0) strncat(DATE_FMT, "04", 2);
        else if(strncmp(MONTH, "Mai", 3) == 0) strncat(DATE_FMT, "05", 2);
        else if(strncmp(MONTH, "Jun", 3) == 0) strncat(DATE_FMT, "06", 2);
        else if(strncmp(MONTH, "Jul", 3) == 0) strncat(DATE_FMT, "07", 2);
        else if(strncmp(MONTH, "Aug", 3) == 0) strncat(DATE_FMT, "08", 2);
        else if(strncmp(MONTH, "Sep", 3) == 0) strncat(DATE_FMT, "09", 2);
        else if(strncmp(MONTH, "Oct", 3) == 0) strncat(DATE_FMT, "10", 2);
        else if(strncmp(MONTH, "Nov", 3) == 0) strncat(DATE_FMT, "11", 2);
        else if(strncmp(MONTH, "Dez", 3) == 0) strncat(DATE_FMT, "12", 2);
        else strncat(DATE_FMT, "??", 2);

        // DAY
        strncat(DAY, &(__DATE__[4]),2);
        if (DAY[0] == ' '){
            strncat(DATE_FMT, "0", 1);
            strncat(DATE_FMT, &(DAY[1]),1);
        } else {
            strncat(DATE_FMT, DAY,2);
        }

        return DATE_FMT;
    }

    const char* TIME() {
        static char HHMMSS[7];
        memset(HHMMSS,0x0,sizeof(HHMMSS));
        strncat(HHMMSS, &(__TIME__[0]),2);
        strncat(HHMMSS, &(__TIME__[3]),2);
        strncat(HHMMSS, &(__TIME__[6]),2);
        return HHMMSS;
    }

    const char* BUILD_NUMBER() {
        static char BUILD_NUMBER_FMT[20];
        memset(BUILD_NUMBER_FMT,0x0,sizeof(BUILD_NUMBER_FMT));
        strncat(BUILD_NUMBER_FMT, DATE()    , sizeof(BUILD_NUMBER_FMT));
        strncat(BUILD_NUMBER_FMT, TIME()    , sizeof(BUILD_NUMBER_FMT));
        return BUILD_NUMBER_FMT;
    }


#elif (MYBUILD_NUMBER_SVN_REV)
#define BUILD_NUMBER_STR() BUILD_NUMBER
    const char* BUILD_NUMBER =
    __DATE__"-"
    __TIME__"-"
    #include "GlobalRevNumber.txt"
    ;


#elif (MYBUILD_NUMBER_TIMESTAMP_SVN_REV)
#define BUILD_NUMBER_STR() BUILD_NUMBER()
    #include <string.h>
    const char* DATE() {
        static char MONTH[5];
        memset(MONTH,0x0,sizeof(MONTH));

        static char DAY[3];
        memset(DAY,0x0,sizeof(DAY));

        static char BUILD_NUMBER[10];
        memset(BUILD_NUMBER,0x0,sizeof(BUILD_NUMBER));

        // YEAR
        strncat(BUILD_NUMBER, &(__DATE__[7]),4);

        // MONTH
        strncat(MONTH, &(__DATE__[0]),3);
        if(strncmp(MONTH, "Jan", 3) == 0) strncat(BUILD_NUMBER, "01", 2);
        else if(strncmp(MONTH, "Fev", 3) == 0) strncat(BUILD_NUMBER, "02", 2);
        else if(strncmp(MONTH, "Mar", 3) == 0) strncat(BUILD_NUMBER, "03", 2);
        else if(strncmp(MONTH, "Apr", 3) == 0) strncat(BUILD_NUMBER, "04", 2);
        else if(strncmp(MONTH, "Mai", 3) == 0) strncat(BUILD_NUMBER, "05", 2);
        else if(strncmp(MONTH, "Jun", 3) == 0) strncat(BUILD_NUMBER, "06", 2);
        else if(strncmp(MONTH, "Jul", 3) == 0) strncat(BUILD_NUMBER, "07", 2);
        else if(strncmp(MONTH, "Aug", 3) == 0) strncat(BUILD_NUMBER, "08", 2);
        else if(strncmp(MONTH, "Sep", 3) == 0) strncat(BUILD_NUMBER, "09", 2);
        else if(strncmp(MONTH, "Oct", 3) == 0) strncat(BUILD_NUMBER, "10", 2);
        else if(strncmp(MONTH, "Nov", 3) == 0) strncat(BUILD_NUMBER, "11", 2);
        else if(strncmp(MONTH, "Dez", 3) == 0) strncat(BUILD_NUMBER, "12", 2);
        else strncat(BUILD_NUMBER, "??", 2);

        // DAY
        strncat(DAY, &(__DATE__[4]),2);
        if (DAY[0] == ' '){
            strncat(BUILD_NUMBER, "0", 1);
            strncat(BUILD_NUMBER, &(DAY[1]),1);
        } else {
            strncat(BUILD_NUMBER, DAY,2);
        }

        return BUILD_NUMBER;
    }

    const char* TIME() {
        static char HHMMSS[7];
        memset(HHMMSS,0x0,sizeof(HHMMSS));
        strncat(HHMMSS, &(__TIME__[0]),2);
        strncat(HHMMSS, &(__TIME__[3]),2);
        strncat(HHMMSS, &(__TIME__[6]),2);
        return HHMMSS;
    }

    const char* REV_NUMBER =
    #include "GlobalRevNumber.txt"
    ;

    const char* BUILD_NUMBER() {
        static char BUILD_NUMBER_FMT[64];
        memset(BUILD_NUMBER_FMT,0x0,sizeof(BUILD_NUMBER_FMT));
        strncat(BUILD_NUMBER_FMT, DATE()    , sizeof(BUILD_NUMBER_FMT));
        strncat(BUILD_NUMBER_FMT, TIME()    , sizeof(BUILD_NUMBER_FMT));
        strncat(BUILD_NUMBER_FMT, REV_NUMBER, sizeof(BUILD_NUMBER_FMT));
        return BUILD_NUMBER_FMT;
    }

#else
#define BUILD_NUMBER_STR() ""
#endif
}

#endif

