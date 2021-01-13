#ifndef GEN_BUILD_NUMBER_STR_H


/**
The macros here can be expanded to help debugging: g++ -E GenBuildNumberStr.h

Options for this build number:
    Enable timestamp as a full number:
            #define GBN_OPTION_TIMESTAMP 1
    
    Compose the build number from timestamp and a file content (build number is up to 96 characters):
            #define GBN_OPTION_INPUT_FILE_NAME "<filename>.dat"


Script example to generate GBN_OPTION_INPUT_FILE_NAME:

    #!/bin/bash
    revnumber=$(svnversion ${PWD})
    echo \"$revnumber\" > GBN_OPTION_INPUT_FILE_NAME

**/

extern "C"{

// If input file is set the timestamp mode shall be used.
#if defined(GBN_OPTION_INPUT_FILE_NAME)
    #define GBN_OPTION_TIMESTAMP 1
#endif


// Provide date and time functions if timestamp mode is enabled.
#if defined(GBN_OPTION_TIMESTAMP)
    #include <string.h>

    const char* GBN_DATE() {
        static char MONTH[5];
        memset(MONTH,0x0,sizeof(MONTH));

        static char DAY[3];
        memset(DAY,0x0,sizeof(DAY));

        static char DATE_FMT[10];
        memset(DATE_FMT,0x0,sizeof(DATE_FMT));

        // YEAR
        int sz = 4;
        strncat(DATE_FMT, &(__DATE__[7]),sz);

        // MONTH
        sz = 2;
        strncat(MONTH, &(__DATE__[0]),3);
        if(strncmp(MONTH, "Jan", 3) == 0) strncat(DATE_FMT, "01", sz);
        else if(strncmp(MONTH, "Fev", 3) == 0) strncat(DATE_FMT, "02", sz);
        else if(strncmp(MONTH, "Mar", 3) == 0) strncat(DATE_FMT, "03", sz);
        else if(strncmp(MONTH, "Apr", 3) == 0) strncat(DATE_FMT, "04", sz);
        else if(strncmp(MONTH, "Mai", 3) == 0) strncat(DATE_FMT, "05", sz);
        else if(strncmp(MONTH, "Jun", 3) == 0) strncat(DATE_FMT, "06", sz);
        else if(strncmp(MONTH, "Jul", 3) == 0) strncat(DATE_FMT, "07", sz);
        else if(strncmp(MONTH, "Aug", 3) == 0) strncat(DATE_FMT, "08", sz);
        else if(strncmp(MONTH, "Sep", 3) == 0) strncat(DATE_FMT, "09", sz);
        else if(strncmp(MONTH, "Oct", 3) == 0) strncat(DATE_FMT, "10", sz);
        else if(strncmp(MONTH, "Nov", 3) == 0) strncat(DATE_FMT, "11", sz);
        else if(strncmp(MONTH, "Dez", 3) == 0) strncat(DATE_FMT, "12", sz);
        else strncat(DATE_FMT, "??", sz);

        // DAY
        strncat(DAY, &(__DATE__[4]),2);
        if (DAY[0] == ' '){
            int sz = 1;
            strncat(DATE_FMT, "0", sz);
            strncat(DATE_FMT, &(DAY[1]),1);
        } else {
            strncat(DATE_FMT, DAY,2);
        }

        return DATE_FMT;
    }

    const char* GBN_TIME() {
        static char HHMMSS[7];
        memset(HHMMSS,0x0,sizeof(HHMMSS));
        int sz = 2;
        strncat(HHMMSS, &(__TIME__[0]),sz);
        strncat(HHMMSS, &(__TIME__[3]),sz);
        strncat(HHMMSS, &(__TIME__[6]),sz);
        return HHMMSS;
    }
#endif


// If the file name to read the version is not provided, print the regular timestamp
#if defined(GBN_OPTION_INPUT_FILE_NAME)
    
    const char* GBN_REV_NUMBER =
    #include GBN_OPTION_INPUT_FILE_NAME
    ;

    #if defined(GBN_OPTION_TIMESTAMP)

        #define GBN_GET_BUILD_NUMBER_STR() GBN_BUILD_NUMBER()
        const char* GBN_BUILD_NUMBER() {
            static char BUILD_NUMBER_FMT[96];
            memset(BUILD_NUMBER_FMT,0x0,sizeof(BUILD_NUMBER_FMT));
            strncat(BUILD_NUMBER_FMT, GBN_DATE(), sizeof(BUILD_NUMBER_FMT));
            strncat(BUILD_NUMBER_FMT, GBN_TIME(), sizeof(BUILD_NUMBER_FMT));
            strncat(BUILD_NUMBER_FMT, GBN_REV_NUMBER, sizeof(BUILD_NUMBER_FMT));
            return BUILD_NUMBER_FMT;
        }
    #endif

#else
    #if defined(GBN_OPTION_TIMESTAMP)

        #define GBN_GET_BUILD_NUMBER_STR() GBN_BUILD_NUMBER()
        const char* GBN_BUILD_NUMBER() {
            static char BUILD_NUMBER_FMT[20];
            memset(BUILD_NUMBER_FMT,0x0,sizeof(BUILD_NUMBER_FMT));
            strncat(BUILD_NUMBER_FMT, GBN_DATE()    , sizeof(BUILD_NUMBER_FMT));
            strncat(BUILD_NUMBER_FMT, GBN_TIME()    , sizeof(BUILD_NUMBER_FMT));
            return BUILD_NUMBER_FMT;
        }
    
    #else

        #define GBN_GET_BUILD_NUMBER_STR() BUILD_NUMBER
            const char* BUILD_NUMBER =
            __DATE__
            " "
            __TIME__
            ;
    
    #endif

#endif

}

#endif

