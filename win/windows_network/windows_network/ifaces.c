#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>

#include "wsa_error.h"

#pragma comment (lib, "Ws2_32.lib")

int ifaces_get(char ***list, int max);
void ifaces_free(char ***list, int total);


void ifaces_free(char ***list, int total)
{
	for (int i = 0; i < total; i++)
	{
		free((*list)[i]);
	}
	free(*list);
	*list = 0;
}

int ifaces_get(char ***list, int max)
{
	int ret = 0;
	DWORD dw_ret = 0;
	int ipv4len = 16;
	int total = 0;
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *item = NULL;
	struct sockaddr_in *ipv4_saddr;

	char *service = "54321";
	char *node = "..localmachine"; //https://docs.microsoft.com/en-us/windows/desktop/api/ws2tcpip/nf-ws2tcpip-getaddrinfo

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	ret = getaddrinfo(node, service, &hints, &result);
	if (ret) {
		PRINT_WSA_ERROR("getaddrinfo failed");
		WSACleanup();
	}
	else
	{
		*list = (char**)calloc(max, sizeof(char*));
		for (item = result; ((item != NULL) && (total < max)); item = item->ai_next) {
			if (item->ai_family == AF_INET)
			{
				ipv4_saddr = (struct sockaddr_in *) item->ai_addr;
				char *ip = calloc(ipv4len, sizeof(char));  //allocate new line
				inet_ntop(AF_INET, &ipv4_saddr->sin_addr, ip, ipv4len);
				(*list)[total++] = ip;
			}
		}

		freeaddrinfo(result);
		ret = total;
	}

	return ret;
}

