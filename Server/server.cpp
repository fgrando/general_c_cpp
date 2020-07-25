#include "server.h"

#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>

#include "hexdump.hpp"

// socket stuff
#ifdef WIN32

#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX(x, y) ((x>y) ? x : y)
#include <cstring>
#endif



Server::Server(TextOutput *out)
    : m_run(false)
    , m_output(out)
{

}

Server::~Server(){
    m_run = false;
}

int Server::Init(std::string ip, int port)
{
    constexpr int OK = 0;
    constexpr int FAIL = -1;

    struct sockaddr_in servaddr;

    // setup TCP socket (listener)
    m_tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_tcpSock < 0){
        return FAIL;
    }

    m_udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_udpSock < 0){
        return FAIL;
    }

    int enable = 1;
    if (setsockopt(m_tcpSock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(int)) < 0){
        return FAIL;
    }

    if (setsockopt(m_udpSock, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(int)) < 0){
        return FAIL;
    }

    // setup server address
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip.c_str());//htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    // TCP binding
    if (bind(m_tcpSock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
        return FAIL;
    }

    if (listen(m_tcpSock, 1) < 0) { // max one TCP client at the time
        return FAIL;
    }

    // UDP binding
    if (bind(m_udpSock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
        return FAIL;
    }

    return OK;
}


void Server::ServerForever()
{
    Print("serving forever!");

    std::stringstream out;
    static unsigned long counter = 0;
    m_run = true;

    char buffer[65535];
    fd_set readSet;
    socklen_t len;
    struct sockaddr_in clientAddr;

    // clear the descriptor set
    FD_ZERO(&readSet);

    while(m_run) {

        out.str("");
        out << ">> " << counter++ << "\n";
        Print(out.str().c_str());

        // populate the set with TCP and UDF sockets
        FD_SET(m_tcpSock, &readSet);
        FD_SET(m_udpSock, &readSet);
        select((MAX(m_tcpSock, m_udpSock) + 1), &readSet, NULL, NULL, NULL);

        // TCP socket is ready: accept the connection and handle it
        if (FD_ISSET(m_tcpSock, &readSet)) {
            len = sizeof(clientAddr);
            int clientSock = accept(m_tcpSock, (struct sockaddr*)&clientAddr, &len);

            // since only one client is allowed we will not fork
            bzero(buffer, sizeof(buffer));

            int bytes = read(clientSock, buffer, sizeof(buffer));

            out.str("");
            out << "[TCP "
                << inet_ntoa(clientAddr.sin_addr) << ":" << clientAddr.sin_port
                << " " << bytes << " bytes]:\n" << hexdump::dump(buffer, bytes);
            Print(out.str().c_str());

            write(clientSock, buffer, bytes);
            close(clientSock);
        }

        // if udp socket is readable receive the message.
        if (FD_ISSET(m_udpSock, &readSet)) {
            len = sizeof(clientAddr);
            bzero(buffer, sizeof(buffer));

            int bytes = recvfrom(m_udpSock, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &len);
            out.str("");
            out << "[UDP "
                << inet_ntoa(clientAddr.sin_addr) << ":" << clientAddr.sin_port
                << " " << bytes << " bytes]:\n" << hexdump::dump(buffer, bytes);

            Print(out.str().c_str());
            sendto(m_udpSock, buffer, bytes, 0, (struct sockaddr*)&clientAddr, sizeof(clientAddr));
        }
    }
}


void Server::Print(const char* out)
{
    //std::cout << out;

    if (m_output != nullptr){
        std::string s(out);
        m_output->Print(s);
    }
}

