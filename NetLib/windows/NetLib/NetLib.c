#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include <NetLib.h>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")


#define DEFAULT_BUFLEN 512

typedef struct winsocket
{
	SOCKET socket; // = INVALID_SOCKET;
}netdata;

netdata config;




void NetLibPrintWsaError()
{
	LPSTR errStr = NULL;
	int ret = 0;
	int len = 0;

	ret = WSAGetLastError();
	len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, ret, 0, (LPSTR)&errStr, 0, 0);
	printf("WSAErr %d: %s", ret, errStr);
	LocalFree(errStr);
}


int NetLibInit()
{
	WSADATA wsaData;
	int ret;
	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (ret != 0) {
		NetLibPrintWsaError();
	}
	return ret;
}

int NetLibOpen(NetLibSetup *setup)
{
	int ret = 0;
	struct addrinfo *result = NULL;
	struct addrinfo	*ptr = NULL;
	struct addrinfo hints;
	char portStr[7] = { 0 };/* max port is 654321\0 = 7 bytes */

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	ret = _snprintf_s(portStr, sizeof(portStr), sizeof(portStr), "%d", setup->port);
	if (ret >= sizeof(portStr))
	{
		PRINT_ERR("invalid port %d", setup->port);
		return ret;
	}

	ret = getaddrinfo(setup->peername, portStr, &hints, &result);
	if (ret != 0) {
		PRINT_ERR("getaddrinfo error %d", ret);
		NetLibPrintWsaError();
		WSACleanup();
		return 1;
	}

	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		config.socket = socket(ptr->ai_family, ptr->ai_socktype,
			ptr->ai_protocol);
		if (config.socket == INVALID_SOCKET) {
			NetLibPrintWsaError();
			WSACleanup();
			return 1;
		}

		ret = connect(config.socket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (ret == SOCKET_ERROR) {
			closesocket(config.socket);
			config.socket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (config.socket == INVALID_SOCKET) {
		PRINT_ERR("Unable to connect to server!");
		WSACleanup();
		return 1;
	}

	return ret;
}


int NetLibSend(char *buff, size_t len)
{
	SOCKADDR_IN ThisSenderInfo;
	int ret = 0;
	int bytes, nlen;
	bytes = 0;
	nlen = 0;

	bytes = send(config.socket, buff, len, 0);
	if (bytes == SOCKET_ERROR)
		NetLibPrintWsaError();
	else
	{
		memset(&ThisSenderInfo, 0, sizeof(ThisSenderInfo));
		nlen = sizeof(ThisSenderInfo);
		getsockname(config.socket, (SOCKADDR *)&ThisSenderInfo, &nlen);
		DEBUG_PRINT("sent %d bytes to %s:%d", bytes, inet_ntoa(ThisSenderInfo.sin_addr), htons(ThisSenderInfo.sin_port));
	}

	return 0;
}

int NetLibRecv(char *buff, size_t len)
{
	int ret = 0;
	memset(buff, 0x0, len);
	ret = recv(config.socket, buff, len, 0);
	if (ret > 0)
	{
		DEBUG_PRINT("Bytes received: %d %s", ret, buff);
	}
	else if (ret == 0)
	{
		DEBUG_PRINT("Connection closed");
	}
	else
	{
		NetLibPrintWsaError();
		return 1;
	}
	return 0;
}

int NetLibClose()
{
	int ret = closesocket(config.socket);
	if (ret != 0)
	{
		NetLibPrintWsaError();
		PRINT_ERR("socket close ret %d", ret);
		return 1;
	}

	return 0;
}


int NetLibFinish()
{
	int ret = WSACleanup();
	if (ret != 0)
	{
		PRINT_ERR("Finish failed %d", ret);
		return 1;
	}

	return 0;
}