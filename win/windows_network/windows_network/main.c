// sources: https://docs.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-recvfrom
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mstcpip.h>

#include <stdio.h>
#include <stdlib.h>


#include <time.h>


#include "wsa_error.h"

#pragma comment (lib, "Ws2_32.lib")



extern int ifaces_get(char ***list, int max);
extern void ifaces_free(char ***list, int total);
extern int timestamp(char *out, int max, int full);
extern int mcast_set_iface(SOCKET sock, char *ifaddr);
extern int mcast_set_ttl(SOCKET sock, int ttl);
extern int mcast_leave(SOCKET sock, char *grpaddr, char *ifaddr);
extern int mcast_join(SOCKET sock, char *grpaddr, char *ifaddr);
extern int mcast_set_loopback(SOCKET sock, int enabled);
extern int doit();//TODO remove this


void usage(char *appname)
{
    printf("usage: \n");

    printf("to send : %s 5 <target ip> <port> <message> [count] [multicast interface] \n", appname);
    printf("to recv : %s 6 <port> [count] [multicast group] [multicast interface] \n", appname);
    printf("pingpong: %s 7 <mcast ip> <port> <message> <multicast interface> \n", appname);
    printf("ifaces  : %s 8\n", appname);
    printf("sniffer : %s 9 <local interface ip>\n", appname);
}

int set_promiscuous_mode(SOCKET sock, int active)
{
    int result = 0;
    DWORD dwValue = (active != 0) ? RCVALL_ON : RCVALL_OFF;

    DWORD dwBytesReturned = 0;
    if (WSAIoctl(sock, SIO_RCVALL, &dwValue, sizeof(dwValue), NULL, 0, &dwBytesReturned, NULL, NULL) == SOCKET_ERROR)
    {
        PRINT_WSA_ERROR("ioctl failed to set SIO_RCVALL");
        result = -1;
    }
    return result;
}

int sock_set_nonblock(SOCKET sock, int nonblock)
{
    int ret = 0;
    ret = ioctlsocket(sock, FIONBIO, &nonblock);
    if (ret != NO_ERROR)
    {
        PRINT_WSA_ERROR("ioctl failed to set FIONBIO");
    }
    return ret;
}

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
        printf("#%d %s\n", i + 1, list[i]);

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

void send_packet(int argc, char *argv[])
{
    SOCKET sock;
    struct sockaddr_in sdest;
    int ret = 0;
    char final_ip[16] = { 0 };
    int multicast = 0; // if set, configure the multicast options
    char *interface_ip = NULL; // if it is multicast, please specify the interface
    int count = 1; // send only one packet
    int port_int = 0; // after conversion


    // parse inputs from user:
    // format: argv[0] 5 <target ip> <port> <message> [count] [multicast interface] 
    if (argc < 5)
    {
        usage(argv[0]);
        return;
    }
    char *target_ip = argv[2]; // can be a regular ip or multicast group
    char *port = argv[3]; // target port
    char *message = argv[4];// message to be sent
    if (argc > 5)
    {
        count = atoi(argv[5]);
    }

    if (argc > 6)
    {
        interface_ip = argv[6];
        multicast = 1;
    }

    // print final configuration 
    port_int = atoi(port);
    printf("send %d pkts ('%s') to %s:%d", count, message, target_ip, port_int);
    if (multicast)
    {
        printf(" (iface %s)", interface_ip);
    }
    printf("\n");


    // open the socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        PRINT_WSA_ERROR("socket() failed");
        exit(EXIT_FAILURE);
    }

    // load the target parameters 
    sdest.sin_family = AF_INET;
    sdest.sin_port = htons(port_int);
    ret = inet_pton(AF_INET, target_ip, &sdest.sin_addr.s_addr);
    if (ret != 1)
    {
        printf("failed to convert %s to ip\n", target_ip);
        exit(EXIT_FAILURE);
    }

    // join the multicast group if we are using multicast
    if (multicast)
    {
        ret = mcast_set_iface(sock, interface_ip);
        ret = mcast_set_ttl(sock, 2);
        ret = mcast_join(sock, target_ip, interface_ip);
        if (ret)
        {
            PRINT_WSA_ERROR("join failed");
            exit(EXIT_FAILURE);
        }
    }

    // finally send data
    inet_ntop(AF_INET, &sdest.sin_addr, final_ip, sizeof(final_ip));
    while ((count--) > 0)
    {
        ret = sendto(sock, message, strlen(message), 0, (struct sockaddr*) &sdest, sizeof(sdest));
        if (ret == SOCKET_ERROR)
        {
            PRINT_WSA_ERROR("sendto() failed ");
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("sent to %s:%d [%s]\n", final_ip, ntohs(sdest.sin_port), message);
        }

        if (count > 0) {
            Sleep(1000);
        }
    }

    // leave the multicast group if we are using multicast
    if (multicast)
    {
        ret = mcast_leave(sock, target_ip, interface_ip);
    }

    // close
    closesocket(sock);
}

void recv_packet(int argc, char *argv[])
{
    SOCKET sock;
    struct sockaddr_in ssource;
    struct sockaddr_in slocal;
    int ret = 0;
    char final_ip[16] = { 0 };
    int multicast = 0; // if set, configure the multicast options
    char *multicast_group = NULL; //if it is multicast, please provide the group to listen to
    char *interface_ip = NULL; // if it is multicast, please specify the interface
    int count = 1; // send only one packet
    int port_int = 0; // after conversion
    int slen = 0;
    char buf[4000] = { 0 };


    // parse inputs from user:
    // format: argv[0] 6 <port> [count] [multicast group] [multicast interface] 
    if (argc < 3)
    {
        usage(argv[0]);
        return;
    }
    char *port = argv[2]; // listen to this port
    if (argc > 3)
    {
        count = atoi(argv[3]);
    }

    if (argc > 4)
    {
        multicast_group = argv[4];
        interface_ip = argv[5];
        multicast = 1;
    }

    // print final configuration 
    port_int = atoi(port);
    printf("recv %d pkts from %d", count, port_int);
    if (multicast)
    {
        multicast_group = argv[4];
        printf(" (from %s iface %s)", multicast_group, interface_ip);
    }
    printf("\n");


    // open the socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        PRINT_WSA_ERROR("socket() failed");
        exit(EXIT_FAILURE);
    }

    // load the target parameters 
    slocal.sin_family = AF_INET;
    slocal.sin_port = htons(port_int);
    slocal.sin_addr.s_addr = htonl(INADDR_ANY); // this way receives from multicast_group and local interface ip

    sock_set_reuse(sock, 1);

    slen = sizeof(slocal);
    ret = bind(sock, (struct sockaddr *)&slocal, slen);
    if (ret == SOCKET_ERROR)
    {
        PRINT_WSA_ERROR("bind failed");
        exit(EXIT_FAILURE);
    }

    if (multicast) // join multicast group, if enabled
    {
        ret = mcast_join(sock, multicast_group, interface_ip);
        if (ret)
        {
            PRINT_WSA_ERROR("join failed");
            exit(EXIT_FAILURE);
        }
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
            printf("recv %s:%d [%s]\n", final_ip, ntohs(ssource.sin_port), buf);
        }
    }

    if (multicast) // leave multicast group, if enabled
    {
        ret = mcast_leave(sock, multicast_group, interface_ip);
        if (ret)
        {
            PRINT_WSA_ERROR("leave failed");
        }
    }


    closesocket(sock);
}

char translate(char c)
{
    char ch = '.';
    if (c >= 0x20 && c <= 0x7E)
        ch = c;
    return ch;
}


void dump_packet(char* buf, int len)
{
    struct ip_hdr{
        UINT8 len;
        UINT8 spare1;
        UINT16 totalLength;
        UINT16 identification;
        UINT16 flags;
        UINT8 ttl;
        UINT8 proto;
        UINT16 checksum;
        UINT32 src;
        UINT32 dst;
    };

    static const UINT8 UDP_PROTO = 0x11;
    struct udp_hdr{
        UINT16 src;
        UINT16 dst;
    };

    static const UINT8 TCP_PROTO = 0x6;
    struct tcp_hdr{
        UINT16 src;
        UINT16 dst;
    };

    printf("\n%d bytes: ", len);

    struct ip_hdr *mask = (struct ip_hdr*)buf;
    struct in_addr src;
    src.s_addr = mask->src;
    struct in_addr dst;
    dst.s_addr = mask->dst;

    if ((UDP_PROTO == mask->proto) ||
        (TCP_PROTO == mask->proto)) {
        
        //will work also for TCP:
        struct udp_hdr *transport = (struct udp_hdr*)(buf + sizeof(struct ip_hdr));
        
        char src_ip[16] = { 0 };
        strncat_s(src_ip, sizeof(src_ip), inet_ntoa(src), sizeof(src_ip));
        char dst_ip[16] = { 0 };
        strncat_s(dst_ip, sizeof(dst_ip), inet_ntoa(dst), sizeof(dst_ip));
        printf("%s %s:%d -> %s:%d\n",
            ((mask->proto == UDP_PROTO) ? "UDP" : "TCP"),
            src_ip, ntohs(transport->src), dst_ip, ntohs(transport->dst));
    }
    else
    {
        printf("src: %s dst: %s\n", inet_ntoa(src), inet_ntoa(dst));
    }

    // carry on the hex dump
    static const int bytes_row = 16;
    int total = len;

    if (total%bytes_row != 0)
    {
        // increment total
        total += (bytes_row - total%bytes_row);
    }

    int line_len = (sizeof(char)* bytes_row) + 1;
    char *line = malloc(line_len);
    memset(line, 0x0, line_len);

    for (int i = 0; i < total; i++)
    {
        if (i < len){
            int value = (int)buf[i] & 0x000000ff;
            printf("%02x ", value);
            line[i%bytes_row] = translate(buf[i]);
        }
        else {
            printf("   ");
            line[i%bytes_row] = '\0';
        }

        if (((i + 1) % bytes_row == 0) ||
            ((i + 1) == total)) /* also print if is the last item */
        {
            printf("| %s\n", line);
            memset(line, 0x0, line_len);
        }
    }
}

void recv_raw(int argc, char *argv[])
{
    printf("In order to know if your system supports RAW sockets, type 'netsh winsock show catalog' in command prompt and look for RAW/IP\n");
    printf("The usage of SOCK_RAW requires administrative privileges: https://docs.microsoft.com/en-us/windows/win32/winsock/tcp-ip-raw-sockets-2)\n");
    printf("This 'raw' socket will not capture other packets (ARP, IPX, and NetBEUI packets, for example) on the interface.\n");

    // see examples at https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ee309610(v=vs.85
    SOCKET sock;
    struct sockaddr_in ssource;
    struct sockaddr_in slocal;
    int ret = 0;
    char final_ip[16] = { 0 };
    char *interface_ip = NULL;
    int slen = 0;
    char buf[65535] = { 0 };


    // parse inputs from user:
    // format: argv[0] 9 ipv4"
    if (argc < 3)
    {
        usage(argv[0]);
        return;
    }
    interface_ip = argv[2]; // bind to this interface

    // open the socket
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_IP); // SIO_RCVALL requires type IPPROTO_IP
    if (sock == INVALID_SOCKET)
    {
        PRINT_WSA_ERROR("socket() failed");
        exit(EXIT_FAILURE);
    }

    //Bind is required to set the interface to promiscuous mode
    slocal.sin_family = AF_INET;
    // slocal.sin_port = htons(port_int); //port is not relevant in raw socket
    slocal.sin_addr.s_addr = inet_addr(interface_ip);// SIO_RCVALL requires INADDR_ANY not to be used

    slen = sizeof(slocal);
    ret = bind(sock, (struct sockaddr *)&slocal, slen);
    if (ret == SOCKET_ERROR)
    {
        PRINT_WSA_ERROR("bind failed");
        exit(EXIT_FAILURE);
    }

    set_promiscuous_mode(sock, 1);

    //so far we will receive data starting from IPv4 length field
    //(missing the first 14 bytes: 12 for mac + ip version)

    // this setting is only useful when sending data (https://docs.microsoft.com/en-us/windows/win32/winsock/tcp-ip-raw-sockets-2)
    //int optval=1;
    //ret = setsockopt(sock, IPPROTO_IP, IP_HDRINCL, (char *)&optval, sizeof optval);
    //if (ret == SOCKET_ERROR)
    //{
    //    PRINT_WSA_ERROR("failed to request IP header");
    //}


    while (1)
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
            //inet_ntop(AF_INET, &ssource.sin_addr, final_ip, sizeof(final_ip));
            //printf("\nrecv %d bytes %s:%d\n", ret, final_ip, ntohs(ssource.sin_port));

            dump_packet(buf, ret);
        }
    }

    closesocket(sock);
}

void pingpong(int argc, char *argv[])
{
    SOCKET sock;
    int slen = 0;
    struct sockaddr_in sdest;
    struct sockaddr_in slocal;
    struct sockaddr_in ssource;
    int ret = 0;
    char final_ip[16] = { 0 };
    int multicast = 0; // if set, configure the multicast options
    int port_int = 0; // after conversion
    char buf[4000] = { 0 };

    srand((unsigned int)time(NULL));

    // parse inputs from user:
    // format: pingpong: %s 7 <mcast ip> <port> <message> <multicast interface> \n
    if (argc < 6)
    {
        usage(argv[0]);
        return;
    }
    char *target_ip = argv[2]; // can be a regular ip or multicast group
    char *port = argv[3]; // target port
    char *message = argv[4];// message to be sent
    char *interface_ip = argv[5];

    multicast = 1;

    // print final configuration 
    port_int = atoi(port);
    printf("send and receive packets.\nSend '%s' to %s:%d using %s and listen to %d\n\n",
        message, target_ip, port_int, interface_ip, port_int);


    // open the socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        PRINT_WSA_ERROR("socket() failed");
        exit(EXIT_FAILURE);
    }


    // load the local parameters 
    slocal.sin_family = AF_INET;
    slocal.sin_port = htons(port_int);
    slocal.sin_addr.s_addr = htonl(INADDR_ANY); // this way receives from multicast_group and local interface ip

    sock_set_reuse(sock, 1);

    slen = sizeof(slocal);
    ret = bind(sock, (struct sockaddr *)&slocal, slen);
    if (ret == SOCKET_ERROR)
    {
        PRINT_WSA_ERROR("bind failed");
        exit(EXIT_FAILURE);
    }

    // load the sending parameters 
    sdest.sin_family = AF_INET;
    sdest.sin_port = htons(port_int);
    ret = inet_pton(AF_INET, target_ip, &sdest.sin_addr.s_addr);
    if (ret != 1)
    {
        printf("failed to convert %s to ip\n", target_ip);
        exit(EXIT_FAILURE);
    }

    // join the multicast group if we are using multicast
    if (multicast)
    {
        ret = mcast_set_iface(sock, interface_ip);
        ret = mcast_set_ttl(sock, 2);
        ret = mcast_set_loopback(sock, 0);
        ret = mcast_join(sock, target_ip, interface_ip);
        if (ret)
        {
            PRINT_WSA_ERROR("join failed");
            exit(EXIT_FAILURE);
        }
    }

    // finally send data
    sock_set_nonblock(sock, 1);

    while (1)
    {
        Sleep(rand() % 3000);

        memset(buf, 0x0, sizeof(buf));
        ret = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &ssource, &slen);
        if (ret == SOCKET_ERROR)
        {
            //PRINT_WSA_ERROR("recvfrom() failed ");
            //exit(EXIT_FAILURE);
            printf(".");
        }
        else
        {
            inet_ntop(AF_INET, &ssource.sin_addr, final_ip, sizeof(final_ip));
            printf("recv %s:%d [%s]\n", final_ip, ntohs(ssource.sin_port), buf);
        }

        Sleep(1000);

        inet_ntop(AF_INET, &sdest.sin_addr, final_ip, sizeof(final_ip));
        ret = sendto(sock, message, strlen(message), 0, (struct sockaddr*) &sdest, sizeof(sdest));
        if (ret == SOCKET_ERROR)
        {
            PRINT_WSA_ERROR("sendto() failed ");
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("sent to %s:%d [%s]\n", final_ip, ntohs(sdest.sin_port), message);
        }

        print_timestamp();
    }

    // leave the multicast group if we are using multicast
    if (multicast)
    {
        ret = mcast_leave(sock, target_ip, interface_ip);
    }

    // close
    closesocket(sock);
}






int main(int argc, char *argv[])
{
    sock_init();

    //static const char **input[] = { "7", "239.0.0.12", "23232", "e430", "10.1.1.21" };
    //pingpong(input, 5);

    if (argc > 1)
    {
        int op = atoi(argv[1]);
        switch (op)
        {
        case 5:
            send_packet(argc, argv);
            break;
        case 6:
            recv_packet(argc, argv);
            break;
        case 7:
            pingpong(argc, argv);
            break;
        case 8:
            print_interfaces_ipv4();
            break;
        case 9:
            recv_raw(argc, argv);
            break;
        default:
            break;
        }
    }
    else
    {
        doit();
        usage(argv[0]);
    }

    sock_finish();
    return 0;
}