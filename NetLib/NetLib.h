#ifndef NETLIB_H
#define NETLIB_H


#define PRINT_ERR(...) do {printf("ERROR [%s:%d] ", __FUNCTION__, __LINE__); fprintf(stdout, __VA_ARGS__); printf("\n"); }while(0)
#define DEBUG_PRINT(...) do {printf("[%s:%d] ", __FUNCTION__, __LINE__); fprintf(stdout, __VA_ARGS__); printf("\n"); }while(0)


typedef  void* NetLibSessionPtr; /* obscure pointer */

typedef enum NetLibSockEnum
{
	NetLibSockNone,
	NetLibSockTcp,
	NetLibSockUdp,
}NetLibSock;

typedef struct NetLibSetupStruct
{
	int port;
	int isServer;
	char *ipAddr;
	char *peername;
	NetLibSock type;

} NetLibSetup;

int NetLibInit();
int NetLibOpen(NetLibSetup *setup);
int NetLibSend(char *buff, size_t len);
int NetLibRecv(char *buff, size_t len);
int NetLibClose();
int NetLibFinish();






#endif /* NETLIB_H */