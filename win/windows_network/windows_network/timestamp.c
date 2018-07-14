#include <time.h>
#include <stdio.h>
#include <string.h>

int timestamp(char *out, int max, int full)
{
	int ret = 0;
	time_t time_now = 0;
	struct tm timeinfo = { 0 };
	
	time(&time_now);
	localtime_s(&timeinfo, &time_now);
	memset(out, 0x0, max);

	if (full)
	{
		ret = snprintf(out, max, "%04d-%02d-%02d.%02d:%02d:%02d",
			(timeinfo.tm_year + 1900), (timeinfo.tm_mon + 1), timeinfo.tm_mday,
			timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	}
	else
	{
		ret = snprintf(out, max, "%02d:%02d:%02d", 
			timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	}
	return ret;
}
