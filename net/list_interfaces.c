#define WIN32_LEAN_AND_MEAN


#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment (lib, "Ws2_32.lib")


void print_wsa_error(char *str)
{
	LPSTR errStr = NULL;
	int ret = 0;
	int len = 0;

	ret = WSAGetLastError();
	len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, ret, 0, (LPSTR)&errStr, 0, 0);
	printf("[%s] WSAErr %d: %s", str, ret, errStr);
	LocalFree(errStr);
}

int main()
{
	DWORD dw_ret = 0;
	int count = 0;
	WSADATA wsa;
	struct addrinfo hints;
	struct addrinfo *ret = NULL;
	struct addrinfo *item = NULL;
	struct sockaddr_in  *ipv4_saddr;
	
	char *service = "8888";
	char *node = "..localmachine";

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		print_wsa_error("wsastartup failed");
		exit(EXIT_FAILURE);
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	dw_ret = getaddrinfo(node, service, &hints, &ret);
	if (dw_ret) {
		print_wsa_error("getaddrinfo failed");
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	for (item = ret; item != NULL; item = item->ai_next) {
		printf("[%d] ", count++);
		switch (item->ai_family) {
		case AF_INET:
			ipv4_saddr = (struct sockaddr_in *) item->ai_addr;
			char id_buf[30] = { 0 };
			inet_ntop(AF_INET, &ipv4_saddr->sin_addr, id_buf, sizeof(id_buf));
			printf("%s\n", id_buf);
			break;
		case AF_INET6:
			printf("AF_INET6\n");
			break;
		case AF_NETBIOS:
			printf("AF_NETBIOS\n");
			break;
		case AF_UNSPEC:
			printf("AF_UNSPEC\n");
			break;
		default:
			printf("other(%ld)\n", item->ai_family);
			break;
		}
	}
	freeaddrinfo(ret);
	
	WSACleanup();

	return 0;
}