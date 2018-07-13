#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")

#define BUFLEN 60000
#define DEFAULT_PORT 55432



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


inline void usage(char *appname)
{
	printf("usage: %s [port] [iface number]\n", appname);
}


inline void display(struct sockaddr_in  *sender, char *buff)
{
	time_t time_now = 0;
	struct tm timeinfo = { 0 };
	char id_buf[20] = { 0 };
	int len = 0;

	time(&time_now);
	len = strlen(buff);
	for (int i = 0; i < len; i++)
	{
		if (!isalnum(buff[i]))
		{
			buff[i] = '_'; // clear nonalpha digits
		}
	}

	localtime_s(&timeinfo, &time_now);
	inet_ntop(AF_INET, &sender->sin_addr, id_buf, sizeof(id_buf));

	printf("%s:%d %02d:%02d:%02d [%s]\n", id_buf, ntohs(sender->sin_port),
		timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, buff);
}


int get_available_ifaces(char ***list)
{
	DWORD dw_ret = 0;
	int max = 10; //interfaces
	int len = 20;
	int total = 0;
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	struct addrinfo *item = NULL;
	struct sockaddr_in *ipv4_saddr;

	char *service = "8888";
	char *node = "..localmachine";

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	dw_ret = getaddrinfo(node, service, &hints, &result);
	if (dw_ret) {
		print_wsa_error("getaddrinfo failed");
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	*list = calloc(max, sizeof(char*));
	for (item = result; ((item != NULL) && (total < max)); item = item->ai_next) {
		if (item->ai_family == AF_INET)
		{
			ipv4_saddr = (struct sockaddr_in *) item->ai_addr;
			char *ip = calloc(len, sizeof(char));  //allocate new line
			inet_ntop(AF_INET, &ipv4_saddr->sin_addr, ip, len);
			(*list)[total++] = ip;
		}
	}
	freeaddrinfo(result);
	return total;
}


void start_listening(struct sockaddr_in *server)
{
	int ret = 0;
	int slen = 0;
	char buf[BUFLEN] = { 0 };
	char final_ip[20] = { 0 }; // ip that will be binded to
	SOCKET sock;
	struct sockaddr_in s_other;

	// create the socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		print_wsa_error("socket() failed");
	}

	// set the reuse option (in case this port is already used by someone the bind will succeed - but then is undefined behavior)
	char enable = 1;
	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
	if (ret < 0)
	{
		print_wsa_error("setsockopt(SO_REUSEADDR) failed");
		exit(EXIT_FAILURE);
	}

	// bind to the address/port defined to run 
	ret = bind(sock, (struct sockaddr *)server, sizeof(*server)); //TODO: set reuse option to avoid trouble binding....
	if (ret == SOCKET_ERROR)
	{
		print_wsa_error("Bind failed");
		exit(EXIT_FAILURE);
	}

	// print the final configuration of ip and port that we will listen to
	inet_ntop(AF_INET, &server->sin_addr, final_ip, sizeof(final_ip));
	printf("listening %s:%d\n", final_ip, ntohs(server->sin_port));
	fflush(stdout);
	while (1)
	{
		memset(buf, 0x0, BUFLEN);
		slen = sizeof(s_other);
		ret = recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr *) &s_other, &slen);
		if (ret == SOCKET_ERROR)
		{
			print_wsa_error("recvfrom() failed");
			exit(EXIT_FAILURE);
		}

		display(&s_other, buf);
	}

	closesocket(sock);
	WSACleanup();
}


int main(int argc, char *argv[])
{
	int ret = 0;
	WSADATA wsa;
	struct sockaddr_in server;

	// load the default parameters 
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(DEFAULT_PORT);

	// first thing to do:
	if (WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		print_wsa_error("wsastartup failed");
		exit(EXIT_FAILURE);
	}

	// check if the user provided a preferred port 
	if (argc > 1)
	{
		server.sin_port = htons( atoi(argv[1]) );
	}

	// check if the user provided a preferred interface
	if (argc > 2)
	{
		int total = 0;
		char **ip_list = NULL;
		int ip_idx = atoi(argv[2]);

		total = get_available_ifaces(&ip_list);
		if (ip_idx >= total)
		{
			printf("%d is unknown. Available addresses: \n", ip_idx);
			for (int i = 0; i < total; i++) //free allocated memory
			{
				printf("%d: %s\n", i, ip_list[i]);
				free(ip_list[i]);
			}
			free(ip_list);
			exit(EXIT_FAILURE);
		}

		ret = inet_pton(AF_INET, ip_list[ip_idx], &server.sin_addr.s_addr);
		if (ret != 1)
		{
			printf("failed to convert %s to ip\n", ip_list[ip_idx]);
			exit(EXIT_FAILURE);
		}

		for (int i = 0; i < total; i++) //free allocated memory
		{
			free(ip_list[i]);
		}
		free(ip_list);
	}

	// print the usage if no parameter wsa provided (maybe the user does not know about)
	if (argc < 3)
	{
		int total = 0;
		char **ip_list = NULL;
		usage(argv[0]);

		total = get_available_ifaces(&ip_list); //free allocated memory below
		for (int i = 0; i < total; i++)
		{
			printf("%d: %s\n", i, ip_list[i]);
			free(ip_list[i]);
		}
		free(ip_list);
		printf("\n\n");
	}



	start_listening(&server);
	return 0;
}


