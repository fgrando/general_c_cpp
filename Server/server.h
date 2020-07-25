#ifndef SERVER_H
#define SERVER_H

#include <string>
#include "textoutput.h"

class Server
{
public:
    Server(TextOutput *out = nullptr);
    ~Server();

    int Init(std::string ip, int port);

    void ServerForever();


private:
    // this function will forward the output to be displayed in some TextOutput
    inline void Print(const char* out);

    bool m_run;
    TextOutput *m_output;

    int m_udpSock;
    int m_tcpSock;
};

#endif // SERVER_H
