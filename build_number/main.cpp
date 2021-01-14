#include <iostream>
using namespace std;

#define BUILD_VER_STR_READ_FILE "svn-rev-number.txt"
#include "BuildVerStrReadFile.h"
#include "BuildVerStrTimestamp.h"

#include "modulea.h"

int main()
{
    BUILD_VER_STR(version);
    BUILD_VER_STR_DATA(fileversion);

    cout << "version is: ";
    cout << "timestamp: " << version;
    cout << "from file: " << fileversion;
    cout << endl;

    modulea();

    return 0;
}
