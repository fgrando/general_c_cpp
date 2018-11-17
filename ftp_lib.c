#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>

/* Prints function name, line, the user message and a new line */
#define DEBUG_PRINT(...) do {printf("[%s:%d] ", __FUNCTION__, __LINE__); fprintf(stdout, __VA_ARGS__); printf("\n"); }while(0)
#define ERROR_PRINT(...) do {printf("[%s:%d] %s: ", __FUNCTION__, __LINE__, strerror(errno)); fprintf(stdout, __VA_ARGS__); printf("\n"); }while(0)

//http://www.nsftools.com/tips/RawFTP.htm
//https://www.ibm.com/support/knowledgecenter/en/ssw_ibm_i_72/rzab6/xnonblock.htm

int ftp_recv(int sock, char *buffer, int len) {
	int ret = 0;
	struct timeval timeout = { 0 };
	fd_set select_set = { 0 };

	FD_ZERO(&select_set);
	FD_SET(sock, &select_set);
	memset(buffer, 0x0, len);
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	ret = select(sock + 1, &select_set, NULL, NULL, &timeout);

	if (ret > 0) {
		ret = recv(sock, buffer, len, 0);
		if (ret < 0) {
			ERROR_PRINT("recv");
			ret = -1;
		}

	} else if (ret < 0) {
		ERROR_PRINT("select failed");
		ret = -2;
	} else {
		DEBUG_PRINT("timeout");
		ret = -3;
	}

	DEBUG_PRINT("ret = %d", ret);
	return ret;
}

void ftp_recv_all(int sock) {
	int ret = 0;
	do {
		char trash[512] = { 0 };
		ret = ftp_recv(sock, trash, sizeof(trash));
		DEBUG_PRINT("%s", trash);
	} while (ret > 0);
}

int ftp_reply(int sock, char *buffer, int len) {
	int ret = 0;
	int bytes = 0;
	int code = 0;

	bytes = ftp_recv(sock, buffer, len);
	if (bytes == -1) {
		ERROR_PRINT("reply");
	} else {
		sscanf(buffer, "%d", &code);
		buffer[strlen(buffer) - 2] = '\0';
		DEBUG_PRINT("%d [%s]", code, buffer);
		ret = code;
	}

	return ret;
}

int ftp_cmd_raw(int sock, char *buffer, int len, char *cmd, char *args) {
	char ftp_cmd[256] = { 0 };
	int ret = 0;

	snprintf(ftp_cmd, sizeof(ftp_cmd), "%s %s\r\n", cmd, args);
	DEBUG_PRINT("cmd..[%s]", ftp_cmd);
	ret = send(sock, ftp_cmd, strlen(ftp_cmd), 0);
	if (ret < 0) {
		ERROR_PRINT("failed to send cmd");
		ret = -1;
	} else {
		ret = ftp_reply(sock, buffer, len);
	}

	DEBUG_PRINT("reply[%d.%s]", ret, buffer);
	return ret;
}

int ftp_cmd(int sock, char *cmd, char *args) {
	char buffer[256] = { 0 };
	int ret = ftp_cmd_raw(sock, buffer, sizeof(buffer), cmd, args);
	DEBUG_PRINT("cmd..[%d.%s]", ret, buffer);
	return ret;
}

int ftp_type(int sock, char *type) {
	int ret = ftp_cmd(sock, "TYPE", type);
	if (200 == ret) {
		DEBUG_PRINT("type set");
	} else {
		ERROR_PRINT("type failed %d", ret);
	}
	return ret;
}

int ftp_login(int sock, char *user, char *pass) {
	int ret = ftp_cmd(sock, "USER", user);
	if (331 == ret) {
		ret = ftp_cmd(sock, "PASS", pass);
		if (230 == ret) {
			DEBUG_PRINT("access granted");
			ret = 1;
		} else {
			DEBUG_PRINT("passwd failed %d", ret);
		}
	} else {
		DEBUG_PRINT("username failed %d", ret);
	}
	return ret;
}

int ftp_pasv(int sock, char *ip, int len, int *port) {
	char buffer[128] = { 0 };
	int ret = ftp_cmd_raw(sock, buffer, sizeof(buffer), "PASV", "");
	if (227 == ret) {
		memset(ip, 0x0, len);
		// parse the result
		int h1, h2, h3, h4, p1, p2;
		char *first = strchr(buffer, '(');
		sscanf(first, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2);

		// save IP and port
		sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
		*port = ((p1 * 256) + p2);

		ret = 0;
	}

	return ret;
}

int ftp_connect(char *ip, int port, int *sock) {
	int ret = 0;
	struct sockaddr_in remote = { 0 };
	(*sock) = socket(AF_INET, SOCK_STREAM, 0);
	if ((*sock) < 0) {
		ERROR_PRINT("failed to create socket");
	}

	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);

	ret = inet_pton(AF_INET, ip, &remote.sin_addr);
	if (ret <= 0) {
		ERROR_PRINT("invalid address");
	}

	ret = connect((*sock), (struct sockaddr *) &remote, sizeof(remote));
	if (ret < 0) {
		ERROR_PRINT("failed to connect");
	}

	return ret;
}

int ftp_open(char *ip, int port, int *sock) {
	int ret = ftp_connect(ip, port, sock);
	if (ret < 0) {
		ERROR_PRINT("failed to connect");
	}

	send(*sock, "", strlen(""), 0);

	char buffer[256] = { 0 };
	int code = ftp_recv(*sock, buffer, sizeof(buffer));
	if (220 == code) {
		DEBUG_PRINT("access ok");
	}

	return ret;
}

int ftp_read(int sock, char *path) {
	FILE *fp = NULL;
	int ret = 0;
	fp = fopen(path, "r");
	if (fp == NULL) {
		ERROR_PRINT("open Error\n");
		return 1;
	}

	char buff[4096] = { 0 };
	int nread = 0;

	nread = fread(buff, 1, sizeof(buff), fp);
	while (nread > 0) {
		ret = send(sock, buff, nread, 0);
		if (ret < 0) {
			ERROR_PRINT("failed to send file");
		}

		memset(buff, 0x0, sizeof(buff));
		nread = fread(buff, 1, sizeof(buff), fp);
	}

	if (ferror(fp)) {
		ERROR_PRINT("error from file");
	}
	fclose(fp);

	return 0;
}

int ftp_write(int sock, char *path) {
	FILE *fp = NULL;

	fp = fopen(path, "w");
	if (fp == NULL) {
		ERROR_PRINT("open Error\n");
		return 1;
	}

	char buff[4096] = { 0 };
	int ret = ftp_recv(sock, buff, sizeof(buff));
	while (ret > 0) {
		if (!fwrite(buff, 1, sizeof(buff), fp)) {
			ERROR_PRINT("write failed");
		}

		ret = ftp_recv(sock, buff, sizeof(buff));
	}

	fclose(fp);
	return 0;
}

int ftp_get(int sock, char *path) {
	int ret = 0;

	puts("\n\ntype");
	ret = ftp_type(sock, "I");
	if (ret < 0)
		exit(1);

	puts("\n\npasv");
	char ip[32] = { 0 };
	int port = 0;
	ret = ftp_pasv(sock, ip, sizeof(ip), &port);
	if (ret < 0)
		exit(1);

	DEBUG_PRINT("data sock %s:%d\n", ip, port);

	puts("\n\nretr");
	ret = ftp_cmd(sock, "RETR", path);
	if (ret < 0)
		exit(1);

	struct sockaddr_in remote = { 0 };
	int data_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (data_sock < 0) {
		ERROR_PRINT("failed to create socket");
		exit(1);
	}

	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	ret = inet_pton(AF_INET, ip, &remote.sin_addr);
	if (ret <= 0) {
		ERROR_PRINT("invalid address");
		exit(1);
	}

	ret = connect(data_sock, (struct sockaddr *) &remote, sizeof(remote));
	if (ret < 0) {
		ERROR_PRINT("failed to connect");
		exit(1);
	}

	ret = ftp_write(data_sock, path);
	close(data_sock);

	puts("trash");
	ftp_recv_all(sock);

	DEBUG_PRINT("write %d", ret);
	return ret;
}

int ftp_put(int sock, char *path) {
	int ret = 0;

	puts("\n\ntype");
	ret = ftp_type(sock, "I");
	if (ret < 0)
		exit(1);

	puts("\n\npasv");
	char ip[32] = { 0 };
	int port = 0;
	ret = ftp_pasv(sock, ip, sizeof(ip), &port);
	if (ret < 0)
		exit(1);

	DEBUG_PRINT("data sock %s:%d\n", ip, port);

	puts("\n\nstor");
	ret = ftp_cmd(sock, "STOR", path);
	if (ret < 0)
		exit(1);

	struct sockaddr_in remote = { 0 };
	int data_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (data_sock < 0) {
		ERROR_PRINT("failed to create socket");
		exit(1);
	}

	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	ret = inet_pton(AF_INET, ip, &remote.sin_addr);
	if (ret <= 0) {
		ERROR_PRINT("invalid address");
		exit(1);
	}

	ret = connect(data_sock, (struct sockaddr *) &remote, sizeof(remote));
	if (ret < 0) {
		ERROR_PRINT("failed to connect");
		exit(1);
	}

	ret = ftp_read(data_sock, path);
	close(data_sock);

	puts("trash");
	ftp_recv_all(sock);

	DEBUG_PRINT("read %d", ret);
	return ret;
}

int ftp_quit(int sock) {
	int ret = ftp_cmd(sock, "QUIT", "");
	if (221 != ret) {
		DEBUG_PRINT("FAILED to quit %d", ret);
	}

	return ret;
}

int main(int argc, char const *argv[]) {
	int ret = 0;
	int sock = 0;

	puts("\n\nconnect");
	ret = ftp_open("192.168.1.13", 2121, &sock);
	if (ret < 0)
		exit(1);

	puts("\n\nlogin");
	ret = ftp_login(sock, "anonymous", "nopass");
	if (ret < 0)
		exit(1);

	puts("\n\nget");
	ret = ftp_get(sock, "deleteme.c");

	puts("\n\nput");
	ret = ftp_put(sock, "testfile5.txt");

	puts("\n\nquit");
	ret = ftp_quit(sock);

	while (0) {
		puts("**");
		char inbuff[512] = { 0 };

		printf(">");
		fgets(inbuff, sizeof(inbuff), stdin);
		inbuff[strlen(inbuff) - 1] = '\r';
		inbuff[strlen(inbuff)] = '\n';
		printf("sent [%s]\n", inbuff);

		send(sock, inbuff, strlen(inbuff), 0);

		char outbuff[2048] = { 0 };
		ftp_recv(sock, outbuff, sizeof(outbuff));

	}
	return 0;
}

