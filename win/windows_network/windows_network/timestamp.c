#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int timestamp(char *buff, int len, int precise)
{
	int ret = 0;
	time_t time_now = 0;
	struct tm timeinfo = { 0 };
	
	time(&time_now);
	localtime_s(&timeinfo, &time_now);
    memset(buff, 0x0, len);

    if (precise)
	{
        ret = _snprintf_s(buff, (size_t)len, (size_t)len, "%04d-%02d-%02d.%02d:%02d:%02d",
			(timeinfo.tm_year + 1900), (timeinfo.tm_mon + 1), timeinfo.tm_mday,
			timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	}
	else
	{
        ret = _snprintf_s(buff, (size_t)len, (size_t)len, "%02d:%02d:%02d",
			timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	}
	return ret;
}
