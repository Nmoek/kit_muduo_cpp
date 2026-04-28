/**
 * @file inet_address.cpp
 * @brief 网络地址类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:37:00
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/inet_address.h"
#include "net/net_log.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>

namespace kit_muduo {

InetAddress::InetAddress(uint16_t port, std::string ip)
{
    bzero((void *)&_addr, sizeof(_addr));
    _addr.sin_family = AF_INET;
    _addr.sin_port = ::htons(port);
    _addr.sin_addr.s_addr = ::inet_addr(ip.c_str());
}

InetAddress::InetAddress(const struct sockaddr_in &addr)
    :_addr(addr)
{
}

uint16_t InetAddress::toPort() const
{
    return ::ntohs(_addr.sin_port);
}
std::string InetAddress::toIp() const
{
    char buf[32] = {0};
    ::inet_ntop(AF_INET , &_addr.sin_addr, buf, sizeof(_addr));
    return buf;
}
std::string InetAddress::toIpPort() const
{
    std::string res;
    res = toIp();
    res += ":";
    res += std::to_string(toPort());
    return res;
}

InetAddress InetAddress::GetLocalAddr(int32_t fd)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if(::getsockname(fd, (struct sockaddr*)&addr, &len) < 0)
    {
        NET_F_ERROR("inet_addr", "::getsockname error! %d:%s \n", errno, strerror(errno));
        return InetAddress();
    }

    return InetAddress(addr);
}

InetAddress InetAddress::GetPeerAddr(int32_t fd)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if(::getpeername(fd, (struct sockaddr*)&addr, &len) < 0)
    {
        NET_F_ERROR("inet_addr", "::getpeername error! fd[%d], %d:%s \n", fd, errno, strerror(errno));
        return InetAddress();
    }
    return InetAddress(addr);
}

InetAddress InetAddress::GetInterfaceIpv4(const std::string& iname)
{
    if(iname.empty())
    {
        NET_F_ERROR("inet_addr", "inet interface name is empty!\n");
        return InetAddress();
    }
    struct ifaddrs *ifaddrs = nullptr;
    struct sockaddr_in tar_addr = {0};

    if(::getifaddrs(&ifaddrs) < 0)
    {
        NET_F_ERROR("inet_addr", "::getifaddrs error! %d:%s \n", errno, strerror(errno));
        return InetAddress();
    }

    for(auto it = ifaddrs; it != nullptr; it = it->ifa_next)
    {
        if(it->ifa_addr == nullptr)
        {
            continue;
        }
        if(it->ifa_name != iname)
        {
            continue;
        }

        if(it->ifa_addr->sa_family == AF_INET)
        {
            tar_addr = *(reinterpret_cast<struct sockaddr_in*>(it->ifa_addr));
            break;
        }
        else
        {
            NET_F_WARN("inet_addr", "find eth %s, but not ipv4!! \n", iname.c_str());
        }
    }
    freeifaddrs(ifaddrs);
    return InetAddress(tar_addr);
}


}   //kit_muduo