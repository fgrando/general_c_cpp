#include "iface.h"

// based on example from MS:
// https://docs.microsoft.com/pt-br/windows/desktop/api/iphlpapi/nf-iphlpapi-getadaptersaddresses

#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>//malloc
#pragma comment(lib, "IPHLPAPI.lib")

#include <WS2tcpip.h>//inet_ntop
#pragma comment(lib, "Ws2_32.lib")//inet_ntop

void print_addr(const ipAddr *ip)
{
	switch (ip->version)
	{
	case ipVerV4:
		printf("%d.%d.%d.%d", ip->ip.v4[0], ip->ip.v4[1], ip->ip.v4[2], ip->ip.v4[3]);
		break;
	case ipVerV6:
		printf("%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
			ip->ip.v6[0], ip->ip.v6[1], ip->ip.v6[2], ip->ip.v6[3],
			ip->ip.v6[4], ip->ip.v6[5], ip->ip.v6[6], ip->ip.v6[7]);
		break;
	default:
		printf("unknown\n");
		return;
	}
	printf("/%d\n", ip->prefix);
}

void print_mac(const uint8_t *mac, int max)
{
	for (int i = 0; i < max; i++)
	{
		printf("%02X", mac[i]);
		if (i < (max - 1))
		{
			printf("-");
		}
	}
	printf("\n");
}

void print_it(const iface *nic)
{
	printf("%s: %s\n", nic->name, nic->description);
	printf("mtu: %d\n", nic->mtu);
	
	printf("type: ");
	switch (nic->type)
	{
	case ifTypeEthernet:
		printf("eth\n");
		break;
	case ifTypeIeee80211:
		printf("wifi\n");
		break;
	case ifTypeLoopback:
		printf("loopback\n");
		break;
	default:
		printf("unknown\n");
	}

	printf("mac: ");
	print_mac(nic->mac, IFACE_MAX_MAC_LEN);

	if (nic->gatewayCount)
	{
		printf("\ngateways:\n");
        for (unsigned int i = 0; i < nic->gatewayCount; i++)
		{
			print_addr(&nic->gateway[i]);
		}
	}

	if (nic->ipCount)
	{
		printf("\nip addresses:\n");
        for (unsigned int i = 0; i < nic->ipCount; i++)
		{
			switch (nic->ip[i].version)
			{
			case ipVerV4:
				printf("ver 4 ");
				break;
			case ipVerV6:
				printf("ver 6 ");
				break;
			default:
				printf("unknown ");
			}
			print_addr(&nic->ip[i]);
		}
	}

	if (nic->multicastCount)
	{
		printf("\nmulticast addresses:\n");
		for (unsigned int i = 0; i < nic->multicastCount; i++)
		{
			print_addr(&nic->multicast[i]);
		}
	}
}

int convert(SOCKET_ADDRESS addr, ipAddr *ip)
{
	int ret = -1;
	if (addr.lpSockaddr->sa_family == AF_INET)
	{
		struct sockaddr_in *sa_in = (struct sockaddr_in *)addr.lpSockaddr;

		ip->prefix = 0x0;
		ip->version = ipVerV4;
		ip->ip.v4[0] = sa_in->sin_addr.S_un.S_un_b.s_b1;
		ip->ip.v4[1] = sa_in->sin_addr.S_un.S_un_b.s_b2;
		ip->ip.v4[2] = sa_in->sin_addr.S_un.S_un_b.s_b3;
		ip->ip.v4[3] = sa_in->sin_addr.S_un.S_un_b.s_b4;
		ret = 4;
	}
	
	if (addr.lpSockaddr->sa_family == AF_INET6)
	{
		struct sockaddr_in6 *sa_in6 = (struct sockaddr_in6 *)addr.lpSockaddr;

		ip->prefix = 0x0;
		ip->version = ipVerV6;
		ip->ip.v6[0] = sa_in6->sin6_addr.u.Word[0];
		ip->ip.v6[1] = sa_in6->sin6_addr.u.Word[1];
		ip->ip.v6[2] = sa_in6->sin6_addr.u.Word[2];
		ip->ip.v6[3] = sa_in6->sin6_addr.u.Word[3];
		ip->ip.v6[4] = sa_in6->sin6_addr.u.Word[4];
		ip->ip.v6[5] = sa_in6->sin6_addr.u.Word[5];
		ip->ip.v6[6] = sa_in6->sin6_addr.u.Word[6];
		ip->ip.v6[7] = sa_in6->sin6_addr.u.Word[7];
		ret = 6;
	}

	return ret;
}



iface *currentIface = NULL;
iface ifaceList[100] = { 0 }; // max 100 interfaces will be stored
int ifaceListCount = 0;

#define IFACE_ERR_OUTOFMEM -1

int doit()
{
	ULONG family = AF_UNSPEC; // AF_INET, AF_INET6;
	ULONG flags =
		GAA_FLAG_INCLUDE_PREFIX | // mask
		GAA_FLAG_INCLUDE_GATEWAYS; // gateway info 

	DWORD dwSize = 0;
	DWORD dwRetVal = 0;

	unsigned int i = 0;

	LPVOID lpMsgBuf = NULL;
	ULONG outBufLen = 0;
	ULONG Iterations = 0;


	PIP_ADAPTER_ADDRESSES pAddresses = NULL;
	PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
	PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;
	PIP_ADAPTER_ANYCAST_ADDRESS pAnycast = NULL;
	PIP_ADAPTER_MULTICAST_ADDRESS pMulticast = NULL;
//	IP_ADAPTER_DNS_SERVER_ADDRESS *pDnServer = NULL;
	PIP_ADAPTER_GATEWAY_ADDRESS pGateway = NULL;
	IP_ADAPTER_PREFIX *pPrefix = NULL;


	// Allocate a 15 KB buffer to start with.
	outBufLen = 150000;

	do {

		pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(outBufLen);
		if (pAddresses == NULL) {
			printf
			("Memory allocation failed for IP_ADAPTER_ADDRESSES struct\n");
			return IFACE_ERR_OUTOFMEM;
		}

		dwRetVal =
			GetAdaptersAddresses(family, flags, NULL, pAddresses, &outBufLen);

		if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
			free(pAddresses);
			pAddresses = NULL;
		}
		else {
			break;
		}

		Iterations++;

	} while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < 3/*MAX_TRIES*/));

	if (dwRetVal == NO_ERROR) {
		// If successful, output some information from the data we received
		pCurrAddresses = pAddresses;
		while (pCurrAddresses) {

			currentIface = &ifaceList[ifaceListCount++];
#if 0
			printf("\tLength of the IP_ADAPTER_ADDRESS struct: %ld\n", pCurrAddresses->Length);
			printf("\tIfIndex (IPv4 interface): %u\n", pCurrAddresses->IfIndex);
			printf("\tAdapter name: %s\n", pCurrAddresses->AdapterName);
#endif

			pGateway = pCurrAddresses->FirstGatewayAddress;
			if (pGateway != NULL) {
				ipAddr *ip = &(currentIface->gateway[currentIface->gatewayCount++]);
				convert(pGateway->Address, ip);
				pGateway = pGateway->Next;
			}

			pUnicast = pCurrAddresses->FirstUnicastAddress;
			if (pUnicast != NULL) {
				for (i = 0; (pUnicast != NULL) && (i < IFACE_MAX_IP_PER_IFACE); i++) //limit by the max
				{
					ipAddr *ip = &(currentIface->ip[currentIface->ipCount++]);
					convert(pUnicast->Address, ip);
					ip->prefix = pUnicast->OnLinkPrefixLength;
					pUnicast = pUnicast->Next;
				}
			}

			pMulticast = pCurrAddresses->FirstMulticastAddress;
			if (pMulticast) {
				for (i = 0; (pMulticast != NULL) && (i < IFACE_MAX_MCASTIP_PER_IFACE); i++)
				{
					ipAddr *ip = &(currentIface->multicast[currentIface->multicastCount++]);
					convert(pMulticast->Address, ip);
					pMulticast = pMulticast->Next;
				}
			}

			// physical address
			if (pCurrAddresses->PhysicalAddressLength != 0) {

				for (int j = 0; j < IFACE_MAX_MAC_LEN; j++)
				{
					currentIface->mac[j] =
						(uint8_t)pCurrAddresses->PhysicalAddress[j];
				}
			}
#if 0
			// ignore anycast
			pAnycast = pCurrAddresses->FirstAnycastAddress;
			// ignore dns
			pDnServer = pCurrAddresses->FirstDnsServerAddress;

			printf("\tDNS Suffix: %wS\n", pCurrAddresses->DnsSuffix);
			printf("\tDescription: %wS\n", pCurrAddresses->Description);
			printf("\tFriendly name: %wS\n", pCurrAddresses->FriendlyName);
			printf("\tFlags: %ld\n", pCurrAddresses->Flags);
			printf("\tMtu: %lu\n", pCurrAddresses->Mtu);
			printf("\tIfType: %ld\n", pCurrAddresses->IfType);
			printf("\tOperStatus: %ld\n", pCurrAddresses->OperStatus);
			printf("\tIpv6IfIndex (IPv6 interface): %u\n",
				pCurrAddresses->Ipv6IfIndex);
			printf("\tZoneIndices (hex): ");
			for (i = 0; i < 16; i++)
				printf("%lx ", pCurrAddresses->ZoneIndices[i]);
			printf("\n");
			printf("\tTransmit link speed: %I64u\n", pCurrAddresses->TransmitLinkSpeed);
			printf("\tReceive link speed: %I64u\n", pCurrAddresses->ReceiveLinkSpeed);

			pPrefix = pCurrAddresses->FirstPrefix;
			if (pPrefix) {
				for (i = 0; pPrefix != NULL; i++)
					pPrefix = pPrefix->Next;
				printf("\tNumber of IP Adapter Prefix entries: %d\n", i);
			}
			else
				printf("\tNumber of IP Adapter Prefix entries: 0\n");
			printf("\n");
#endif

			// ifindex
			currentIface->index = pCurrAddresses->IfIndex;

			// name (using FriendlyName instead of AdapterName)
			int tmp = 0;
			wcstombs_s(&tmp, currentIface->name, IFACE_MAX_NAME_LEN,
				pCurrAddresses->FriendlyName, lstrlenW(pCurrAddresses->FriendlyName));
			//wcstombs(currentIface->name, pCurrAddresses->FriendlyName, IFACE_MAX_NAME_LEN);

			// description
			tmp = 0;
			wcstombs_s(&tmp, currentIface->description, IFACE_MAX_DESC_LEN,
				pCurrAddresses->Description, lstrlenW(pCurrAddresses->Description));
			//wcstombs(currentIface->description, pCurrAddresses->Description, IFACE_MAX_DESC_LEN);

			// mtu
			currentIface->mtu = pCurrAddresses->Mtu;

			// operation status
			switch (pCurrAddresses->OperStatus)
			{
			case IfOperStatusUp:
				currentIface->status = ifTypeEthernet;
				break;

			case IfOperStatusDown:
				currentIface->status = ifTypeLoopback;
				break;

			default:
				currentIface->status = ifOperStatusOther;
				break;
			}

			// mac address 
			switch (pCurrAddresses->IfType)
			{
			case IF_TYPE_ETHERNET_CSMACD:
				currentIface->type = ifTypeEthernet;
				break;

			case IF_TYPE_SOFTWARE_LOOPBACK:
				currentIface->type = ifTypeLoopback;
				break;

			case IF_TYPE_IEEE80211:
				currentIface->type = ifTypeIeee80211;
				break;

			default:
				currentIface->type = ifTypeOther;
				break;
			}

			pCurrAddresses = pCurrAddresses->Next;
		}
	}
	else {
		printf("Call to GetAdaptersAddresses failed with error: %d\n",
			dwRetVal);
		if (dwRetVal == ERROR_NO_DATA)
			printf("\tNo addresses were found for the requested parameters\n");
		else {

			if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, dwRetVal, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				// Default language
				(LPTSTR)&lpMsgBuf, 0, NULL)) {
				printf("\tError: %s", (char*)lpMsgBuf);
				LocalFree(lpMsgBuf);
				if (pAddresses)
					free(pAddresses);
				exit(1);
			}
		}
	}

	if (pAddresses) {
		free(pAddresses);
	}

	for (int k = 0; k < ifaceListCount; k++)
	{
		printf("-----------------------------------------------------------------------\n");
		print_it(&ifaceList[k]);
		printf("\n\n");
	}
    return 0;
}