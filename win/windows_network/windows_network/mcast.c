#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include "wsa_error.h"

#pragma comment (lib, "Ws2_32.lib") // need to link this

int mcast_set_iface(SOCKET sock, char *ifaddr);
int mcast_set_ttl(SOCKET sock, int ttl);
int mcast_leave(SOCKET sock, char *grpaddr, char *ifaddr);
int mcast_join(SOCKET sock, char *grpaddr, char *ifaddr);
int mcast_set_loopback(SOCKET sock, int enabled);
inline int mcast_config(SOCKET sock, char *group_addr, char *iface_addr, int opt);



inline int mcast_config(SOCKET sock, char *group_addr, char *iface_addr, int opt)
{
	int ret = 0;
	if (IP_ADD_MEMBERSHIP == opt || IP_DROP_MEMBERSHIP == opt)
	{
		struct ip_mreq mreq;
		ret = inet_pton(AF_INET, group_addr, &mreq.imr_multiaddr.s_addr);
		if (ret != 1)
		{
			PRINT_WSA_ERROR("inet_pton: failed to convert group address (%s)", group_addr);
		}

		ret = inet_pton(AF_INET, iface_addr, &mreq.imr_interface.s_addr);
		if (ret != 1)
		{
			PRINT_WSA_ERROR("inet_pton: failed to convert interface address (%s)", iface_addr);
		}

		ret = setsockopt(sock, IPPROTO_IP, opt, (char *)&mreq, sizeof(mreq));
	}
	return ret;
}

int mcast_join(SOCKET sock, char *grpaddr, char *ifaddr)
{
	return mcast_config(sock, grpaddr, ifaddr, IP_ADD_MEMBERSHIP);
}

int mcast_leave(SOCKET sock, char *grpaddr, char *ifaddr)
{
	return mcast_config(sock, grpaddr, ifaddr, IP_DROP_MEMBERSHIP);
}

int mcast_set_iface(SOCKET sock, char *ifaddr)
{
	int ret = 0;
	struct in_addr iface;

	ret = inet_pton(AF_INET, ifaddr, &iface.s_addr);
	if (ret != 1)
	{
		PRINT_WSA_ERROR("inet_pton: failed to convert interface address (%s)", ifaddr);
	}
	else
	{
		ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (char *)&iface, sizeof(iface));
	}

	return ret;
}

int mcast_set_ttl(SOCKET sock, int ttl)
{
	int ret = 0;
	ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(ttl));

	if (ret == SOCKET_ERROR)
	{
		PRINT_WSA_ERROR("failed to set mcast ttl option to %d", ttl);
	}
	return ret;
}

int mcast_set_loopback(SOCKET sock, int enabled)
{
	int ret = 0;
	ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&enabled, sizeof(enabled));

	if (ret == SOCKET_ERROR)
	{
		PRINT_WSA_ERROR("failed to set mcast loopback option to %d", enabled);
	}
	return ret;
}

