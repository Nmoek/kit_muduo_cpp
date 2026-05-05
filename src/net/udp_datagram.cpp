/**
 * @file udp_datagram.cpp
 * @brief UDP报文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-27 15:23:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/udp_datagram.h"
#include "net/inet_address.h"
#include "net/net_log.h"
#include "net/socket.h"

#include <cstring>
#include <memory>
#include <sys/socket.h>

namespace kit_muduo {

UdpDatagram::UdpDatagram(const std::string& name, int32_t sockfd)
    :name_(name)
    ,socket_(std::make_unique<Socket>(sockfd, Socket::Type::UDP))
{
    CONN_F_DEBUG("UdpDatagram create: name[%s], fd[%d]\n", name.c_str(), sockfd);
}

UdpDatagram::~UdpDatagram() = default;

bool UdpDatagram::bind(const InetAddress &local_addr) 
{ 
    if(!socket_->isBind() && !socket_->bindAddress(local_addr))
    {
        return false;
    }
    return true;
}

ssize_t UdpDatagram::sendTo(const std::vector<uint8_t> &message, const InetAddress &peer_addr)
{
    return sendTo(message.data(), message.size(), peer_addr);
}

ssize_t UdpDatagram::sendTo(const void* buf, size_t len, const InetAddress &peer_addr)
{
    ssize_t n = ::sendto(socket_->fd(),
        buf,
        len,
        0,
        reinterpret_cast<const sockaddr*>(peer_addr.getSockAddr()),
        static_cast<socklen_t>(sizeof(sockaddr_in)));
    if(n < 0)
    {
        UDP_F_ERROR("sendTo error! name[%s], fd[%d], %s, %d:%s\n",
            name_.c_str(),
            socket_->fd(),
            peer_addr.toIpPort().c_str(),
            errno,
            strerror(errno));
        return -1;
    }
    return n;
}

ssize_t UdpDatagram::recvFrom(std::vector<uint8_t> &message, InetAddress &peer_addr)
{
    if(message.size() < kMTU)
    {
        message.resize(kMTU);
    }

    ssize_t recv_size = recvFrom(message.data(), message.size(), peer_addr);
    if(recv_size >= 0)
    {
        message.resize(static_cast<size_t>(recv_size));
    }

    return recv_size;
}

ssize_t UdpDatagram::recvFrom(void *buf, size_t buf_len, InetAddress &peer_addr)
{
    struct sockaddr_in addr = {0};
    socklen_t addr_len = sizeof(addr);

    ssize_t n = ::recvfrom(socket_->fd(), buf, buf_len, 0, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if(n < 0)
    {
        UDP_F_ERROR("recvfrom error! fd[%d], %d:%s \n", socket_->fd(), errno, strerror(errno));
        return -1;
    }
    peer_addr.setSockAddr(addr);
    return n;
}

}
