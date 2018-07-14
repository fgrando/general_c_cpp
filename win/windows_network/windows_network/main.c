#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>


#include "wsa_error.h"

#pragma comment (lib, "Ws2_32.lib")



extern int ifaces_get(char ***list, int max);
extern void ifaces_free(char ***list, int total);
extern int timestamp(char *out, int max, int full);
extern int mcast_set_iface(SOCKET sock, char *ifaddr);
extern int mcast_set_ttl(SOCKET sock, int ttl);
extern int mcast_leave(SOCKET sock, char *grpaddr, char *ifaddr);
extern int mcast_join(SOCKET sock, char *grpaddr, char *ifaddr);



int sock_init()
{
	WSADATA wsa;
	int ret = 0;
	
	ret = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (ret)
	{
		PRINT_WSA_ERROR("wsastartup failed");
	}
	return ret;
}

int sock_finish()
{
	return WSACleanup();
}

void print_timestamp()
{
	char stamp[64] = { 0 };
	timestamp(stamp, 64, 1);
	printf("%s\n", stamp);
}

void print_interfaces_ipv4()
{
	char **list;
	int max = 10;
	int total = 0;

	total = ifaces_get(&list, max);
	for (int i = 0; i < total; i++)
		printf("%d %s\n", i, list[i]);

	ifaces_free(&list, total);
}

int sock_set_reuse(SOCKET sock, int enable)
{
	int ret = 0;
	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(int));
	if (ret)
	{
		PRINT_WSA_ERROR("setsockopt(SO_REUSEADDR) failed");
	}
	return ret;
}

void send_udp_packet(int argc, char *argv[])
{
	SOCKET sock;
	int port = 7788;
	char *ip = "192.168.56.101";
	char *msg = " hello_from_udp";
	struct sockaddr_in sdest;
	int ret = 0;
	int slen = 0;
	int count = 5;
	char final_ip[16] = { 0 };

	// open the socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		PRINT_WSA_ERROR("socket() failed");
		exit(EXIT_FAILURE);
	}

	// load the target parameters 
	sdest.sin_family = AF_INET;
	sdest.sin_port = htons(port);
	ret = inet_pton(AF_INET, ip, &sdest.sin_addr.s_addr);
	if (ret != 1)
	{
		printf("failed to convert %s to ip\n", ip);
		exit(EXIT_FAILURE);
	}
	
	slen = sizeof(sdest);
	inet_ntop(AF_INET, &sdest.sin_addr, final_ip, sizeof(final_ip));
	
	while ((count--) > 0)
	{
		char buf[128] = { 0 };
		timestamp(buf, 64, 0);
		strcat_s(buf, 128, msg); //timestamp + message
		ret = sendto(sock, buf, strlen(buf), 0, (struct sockaddr*) &sdest, slen);
		if (ret == SOCKET_ERROR)
		{
			PRINT_WSA_ERROR("sendto() failed ");
			exit(EXIT_FAILURE);
		}
		else
		{
			printf("sent to %s:%d [%s]\n", final_ip, ntohs(sdest.sin_port), buf);
		}
	}

	closesocket(sock);
}

void recv_udp_packet(int argc, char *argv[])
{
	SOCKET sock;
	int port = 7788;
	struct sockaddr_in ssource;
	struct sockaddr_in slocal;
	int ret = 0;
	int slen = 0;
	int count = 5;
	char final_ip[16] = { 0 };
	char buf[4000] = { 0 };

	// open the socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		PRINT_WSA_ERROR("socket() failed");
		exit(EXIT_FAILURE);
	}

	// load the target parameters 
	slocal.sin_family = AF_INET;
	slocal.sin_port = htons(port);
	slocal.sin_addr.s_addr = htonl(INADDR_ANY);

	sock_set_reuse(sock, 1);

	slen = sizeof(slocal);
	ret = bind(sock, (struct sockaddr *)&slocal, slen);
	if (ret == SOCKET_ERROR)
	{
		PRINT_WSA_ERROR("bind failed");
		exit(EXIT_FAILURE);
	}

	while ((count--) > 0)
	{
		memset(buf, 0x0, sizeof(buf));
		ret = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &ssource, &slen);

		if (ret == SOCKET_ERROR)
		{
			PRINT_WSA_ERROR("recvfrom() failed ");
			exit(EXIT_FAILURE);
		}
		else
		{
			inet_ntop(AF_INET, &ssource.sin_addr, final_ip, sizeof(final_ip));
			printf("%s recv %s:%d [%s]\n", buf, final_ip, ntohs(ssource.sin_port), buf);
		}
	}

	closesocket(sock);
}

void send_mcast_packet(int argc, char *argv[])
{
	SOCKET sock;
	int port = 55555;
	char *mcast_group = "239.0.0.1";
	char *mcast_iface = "192.168.56.101";
	char *msg = " hello_from_udp";
	struct sockaddr_in sdest;
	int ret = 0;
	int slen = 0;
	int count = 5;
	char final_ip[16] = { 0 };

	// open the socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		PRINT_WSA_ERROR("socket() failed");
		exit(EXIT_FAILURE);
	}

	// load the target parameters 
	sdest.sin_family = AF_INET;
	sdest.sin_port = htons(port);
	ret = inet_pton(AF_INET, mcast_group, &sdest.sin_addr.s_addr);
	if (ret != 1)
	{
		printf("failed to convert %s to ip\n", mcast_group);
		exit(EXIT_FAILURE);
	}


	ret = mcast_set_iface(sock, mcast_iface);
	ret = mcast_set_ttl(sock, 2);
	ret = mcast_join(sock, mcast_group, mcast_iface);
	if (ret)
	{
		PRINT_WSA_ERROR("join failed");
		exit(EXIT_FAILURE);
	}

	slen = sizeof(sdest);
	inet_ntop(AF_INET, &sdest.sin_addr, final_ip, sizeof(final_ip));

	while ((count--) > 0)
	{
		char buf[128] = { 0 };
		timestamp(buf, 64, 0);
		strcat_s(buf, 128, msg); //timestamp + message
		ret = sendto(sock, buf, strlen(buf), 0, (struct sockaddr*) &sdest, slen);
		if (ret == SOCKET_ERROR)
		{
			PRINT_WSA_ERROR("sendto() failed ");
			exit(EXIT_FAILURE);
		}
		else
		{
			printf("sent to %s:%d [%s]\n", final_ip, ntohs(sdest.sin_port), buf);
		}
	}

	ret = mcast_leave(sock, mcast_group, mcast_iface);

	closesocket(sock);
}

void recv_mcast_packet(int argc, char *argv[])
{
	SOCKET sock;
	int port = 55555;
	char *mcast_group = "239.0.0.1";
	char *mcast_iface = "192.168.56.101";
	struct sockaddr_in ssource;
	struct sockaddr_in slocal;
	int ret = 0;
	int slen = 0;
	int count = 5;
	char final_ip[16] = { 0 };
	char buf[4000] = { 0 };

	// open the socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		PRINT_WSA_ERROR("socket() failed");
		exit(EXIT_FAILURE);
	}

	// load the target parameters 
	slocal.sin_family = AF_INET;
	slocal.sin_port = htons(port);
	slocal.sin_addr.s_addr = htonl(INADDR_ANY);

	sock_set_reuse(sock, 1);

	slen = sizeof(slocal);
	ret = bind(sock, (struct sockaddr *)&slocal, slen);
	if (ret == SOCKET_ERROR)
	{
		PRINT_WSA_ERROR("bind failed");
		exit(EXIT_FAILURE);
	}

	ret = mcast_join(sock, mcast_group, mcast_iface);
	if (ret)
	{
		PRINT_WSA_ERROR("join failed");
		exit(EXIT_FAILURE);
	}

	while ((count--) > 0)
	{
		memset(buf, 0x0, sizeof(buf));
		ret = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &ssource, &slen);

		if (ret == SOCKET_ERROR)
		{
			PRINT_WSA_ERROR("recvfrom() failed ");
			exit(EXIT_FAILURE);
		}
		else
		{
			inet_ntop(AF_INET, &ssource.sin_addr, final_ip, sizeof(final_ip));
			printf("%s recv %s:%d [%s]\n", buf, final_ip, ntohs(ssource.sin_port), buf);
		}
	}

	// exit the group
	ret = mcast_leave(sock, mcast_group, mcast_iface);
	if (ret)
	{
		PRINT_WSA_ERROR("leave failed");
		exit(EXIT_FAILURE);
	}

	closesocket(sock);
}


int main(int argc, char *argv[])
{
	print_timestamp();
	sock_init();
	print_interfaces_ipv4();


	if (argc > 1)
	{
		int op = atoi(argv[1]);
		switch (op)
		{
		case 1:
			send_udp_packet(argc, argv);
			break;
		case 2:
			recv_udp_packet(argc, argv);
			break;
		case 3:
			send_mcast_packet(argc, argv);
			break;
		case 4:
			recv_mcast_packet(argc, argv);
			break;
		default:
			break;
		}
	}
	else
	{
		printf("1: send udp\n");
		printf("2: recv udp\n");
		printf("3: send mcast\n");
		printf("4: recv mcast\n");
	}

	sock_finish();
	return 0;
}