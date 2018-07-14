#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#pragma comment (lib, "Ws2_32.lib") // need to link this

void wsa_error_print()
{
	LPSTR errStr = NULL;
	int ret = 0;
	int len = 0;

	ret = WSAGetLastError();
	len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, ret, 0, (LPSTR)&errStr, 0, 0);
	printf("WSAErr %d: %s", ret, errStr);
	LocalFree(errStr);
}

