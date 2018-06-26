#include <stdio.h>
#include <stdlib.h> // exit
#include <string.h> // memset
#include <unistd.h> // sleep
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void sender(char *ip, int port) {
	struct sockaddr_in saddr;
	size_t saddrlen = 0;
	int sock = 0;
	int bytes = 0;
	char message[20] = { 0 };
	int counter = 0;
	unsigned char ttl = 1;
	unsigned char loop = 1;

	memset((void *) &saddr, 0x0, sizeof(saddr));

	/* set up socket */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);
	saddrlen = sizeof(saddr);

	saddr.sin_addr.s_addr = inet_addr(ip);

	/* not checking setsockopt return - dont try this at home :x */

	/* ttl is 1 by default (kept inside our LAN) */
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

	/* send the data looped back to me if loop = 1 (default) */
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	/*
	 * This configuration can override the OS default multicast interface.
	 * Lets only read the default for OS.
	 */
	struct in_addr interface_addr;
	socklen_t optlen = 0;
	if (getsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, &optlen)
			== 0) {
		printf("sending traffic using interface %s\n",inet_ntoa(interface_addr));
	}
	else
	{
		perror("failed to read current multicast interface");
	}

	while (1) {
		sprintf(message, "i am message %d", counter);
		printf("sent: '%s'\n", message);
		bytes = sendto(sock, message, sizeof(message), 0,
				(struct sockaddr *) &saddr, saddrlen);
		if (bytes < 0) {
			perror("sendto");
			exit(1);
		}
		sleep(5);
		counter++;
	}
}

void receiver(char *ip, int port) {
	struct sockaddr_in saddr;
	socklen_t saddrlen;
	int sock = 0;
	int bytes = 0;
	char message[20] = { 0 };
	struct ip_mreq mreq;
	unsigned char reuse = 1;

	memset((void *) &saddr, 0x0, sizeof(saddr));

	/* set up socket */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);
	saddrlen = sizeof(saddr);

	if (bind(sock, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		perror("bind");
		exit(1);
	}
	mreq.imr_multiaddr.s_addr = inet_addr(ip);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))
			< 0) {
		perror("setsockopt mreq");
		exit(1);
	}
	while (1) {
		bytes = recvfrom(sock, message, sizeof(message), 0,
				(struct sockaddr *) &saddr, &saddrlen);
		if (bytes < 0) {
			perror("recvfrom");
			exit(1);
		} else if (bytes == 0) {
			break;
		}
		printf("received '%s' [%s]\n", message, inet_ntoa(saddr.sin_addr));
	}
}

int main(int argc, char *argv[]) {

	int port = 7655;
	char *ip = "239.0.0.1";
	int server = 0;

	if (argc >= 3) {
		ip = argv[1];
		port = atoi(argv[2]);
		if (argc > 3) {
			server = 1;
		}
	}
	else
	{
		printf("usage: %s <multicast ip> <port> [s]\n", argv[0]);
		printf("default: %s %s %d\n",argv[0], ip, port);
	}

	printf("multicast address %s and port %d\n", ip, port);

	if (server) {
		printf("listening...\n");
		receiver(ip, port);

	} else {
		printf("sending...\n");
		sender(ip, port);
	}

	return 0;
}

