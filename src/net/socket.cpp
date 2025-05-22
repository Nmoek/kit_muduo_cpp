/**
 * @file socket.cpp
 * @brief socket封装
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 23:58:21
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/socket.h"
#include "net/inet_address.h"
#include "net/net_log.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

namespace kit_muduo {

Socket::Socket(int32_t fd)
    :_sockfd(fd)
{

}
Socket::~Socket()
{
    if(_sockfd > 0)
        ::close(_sockfd);
}

void Socket::bindAddress(const InetAddress &addr)
{
    if(::bind(_sockfd, (const struct sockaddr*)addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr))) < 0)
    {
        SOCK_F_FATAL("::bind error! %d:%s \n", errno, strerror(errno));
        abort();
    }
}

void Socket::listen()
{
    if(::listen(_sockfd, 1024) < 0)
    {
        SOCK_F_FATAL("::listen error! %d:%s \n", errno, strerror(errno));
        abort();
    }
}

int32_t Socket::accept(InetAddress *peerAddr)
{
    struct sockaddr_in sockaddr = {0};
    socklen_t len = sizeof(struct sockaddr);
    int32_t fd = -1;
    fd = ::accept(_sockfd, (struct sockaddr*)&sockaddr, &len);
    if(fd < 0)
    {
        SOCK_F_ERROR("::accept error! %d:%s \n", errno, strerror(errno));
        return -1;
    }
    peerAddr->setSockAddr(sockaddr);
    return fd;
}

void Socket::shutdownWrite()
{
    if(::shutdown(_sockfd, SHUT_WR) < 0)
    {
        SOCK_F_ERROR("::shutdown error! %d:%s \n", errno, strerror(errno));
    }
}

void Socket::setTcpNoDelay(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(_sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}
void Socket::setReuseAddr(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}
void Socket::setReusePort(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}

int32_t Socket::Create()
{
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

}   //kit_muduo