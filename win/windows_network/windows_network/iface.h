#ifndef IFACE_H
#define IFACE_H

#include <stdint.h>

#define IFACE_MAX_IP_PER_IFACE 10
#define IFACE_MAX_MCASTIP_PER_IFACE 20
#define IFACE_MAX_GATEWAYIP_PER_IFACE 2
#define IFACE_MAX_NAME_LEN 80 
#define IFACE_MAX_DESC_LEN 80
#define IFACE_MAX_MAC_LEN 6

// version of IP protocol 
typedef enum ipVerEnum
{
	ipVerV4 = 0,
	ipVerV6,
	ipVerInvalid
}ipVer;

// types of interface (cable, wifi)
typedef enum ifTypeEnum
{
	ifTypeEthernet,
	ifTypeIeee80211,
	ifTypeLoopback,
	ifTypeOther
}ifType;

// ip address xxx.yyy.www.zzz plus ipv6
typedef struct ipAddrStruct
{
	union {
		uint8_t v4[4]; //32 bits
		uint16_t v6[8]; //128 bits
	}ip;
	ipVer version;
	uint32_t prefix; //CIDR mask
} ipAddr;

// statuses of this interface (up or down)
typedef enum ifOperStatusEnum
{
	ifOperStatusUp,
	ifOperStatusDown,
	ifOperStatusOther
}ifOperStatus;



typedef struct ifaceStruct
{
	uint64_t mtu;
	uint8_t mac[IFACE_MAX_MAC_LEN];
	uint32_t index;
	ifType type;
	ifOperStatus status;

	char name[IFACE_MAX_NAME_LEN];
	char description[IFACE_MAX_DESC_LEN];

	ipAddr gateway[IFACE_MAX_GATEWAYIP_PER_IFACE];
	uint32_t gatewayCount;
	
	ipAddr ip[IFACE_MAX_IP_PER_IFACE];
	uint32_t ipCount;

	ipAddr multicast[IFACE_MAX_MCASTIP_PER_IFACE];
	uint32_t multicastCount;
} iface;


int doit();

#endif /* IFACE_H */