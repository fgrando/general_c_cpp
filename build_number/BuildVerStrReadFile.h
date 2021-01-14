#ifndef BUILD_VER_STR_READ_FILE_H
#define BUILD_VER_STR_READ_FILE_H

/**
 * Usage example:
 *  #define BUILD_VER_STR_READ_FILE "svn-rev-number.txt"
 *  BUILD_VER_STR_DATA(filecontent);
 *  cout << filecontent << endl;
**/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#ifndef BUILD_VER_STR_READ_FILE
    #error Missing file path definition (#define BUILD_VER_STR_READ_FILE "<filename>.dat")
    #define BUILD_VER_STR_DATA(PTR) const char* PTR = 0;
#else

    static const char* GEN_BUILD_NUMBER_FILE_CONTENT =
        #include BUILD_VER_STR_READ_FILE
    ;

    #define BUILD_VER_STR_DATA(PTR) \
        const char* PTR = GEN_BUILD_NUMBER_FILE_CONTENT;
#endif


#ifdef __cplusplus
}
#endif // __cplusplus
#endif // GEN_BUILD_NUMBER_STR_READ_FILE_H

