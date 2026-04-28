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
#include <fcntl.h>

namespace kit_muduo {

Socket::Socket(int32_t fd, Socket::Type type)
    :sockfd_(fd)
    ,type_(type)
    ,is_bind_(false)
{

}
Socket::~Socket()
{
    if(sockfd_ > 0)
    {
        ::close(sockfd_);
    }
}

int32_t Socket::get() 
{ 
    int32_t fd = sockfd_; 
    sockfd_ = -1;
    return fd; 
}

bool Socket::bindAddress(const InetAddress &addr)
{
    if(::bind(sockfd_, (const struct sockaddr*)addr.getSockAddr(), static_cast<socklen_t>(sizeof(struct sockaddr))) < 0)
    {
        SOCK_F_FATAL("::bind error! %d:%s \n", errno, strerror(errno));
        return false;
    }
    is_bind_ = true;
    return true;
}

void Socket::listen()
{
    if(::listen(sockfd_, 1024) < 0)
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
    fd = ::accept4(sockfd_, (struct sockaddr*)&sockaddr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
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
    // 先关闭 写通道 触发FIN包发送
    if(::shutdown(sockfd_, SHUT_WR) < 0)
    {
        SOCK_F_ERROR("::shutdown error! %d:%s \n", errno, strerror(errno));
    }
}

void Socket::setTcpNoDelay(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

void Socket::setKeepAlive(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

void Socket::setReuseAddr(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}
void Socket::setReusePort(bool on)
{
    int32_t opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}


static void SetSocketNoNBlock(int32_t fd, bool on)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        SOCK_F_ERROR("SetSocketNoNBlock error! %d:%s \n", errno, strerror(errno));
        return;
    }
    if(on)
    {
        flags |= O_NONBLOCK;
    }
    else
    {
        flags &= ~O_NONBLOCK;
    }

    fcntl(fd, F_SETFL, flags);
}

int32_t Socket::CreateTcpIpv4(bool nonblock)
{
    int32_t fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0)
    {
        SOCK_F_FATAL("::socket error! %d:%s \n", errno, strerror(errno));
        abort();
    }
    SetSocketNoNBlock(fd, nonblock);
    return fd;
}

int32_t Socket::CreateUdpIpv4(bool nonblock)
{
    int32_t fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if(fd < 0)
    {
        SOCK_F_FATAL("::socket error! %d:%s \n", errno, strerror(errno));
        abort();
    }

    SetSocketNoNBlock(fd, nonblock);
    return fd;
}


}   //kit_muduo