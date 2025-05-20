/**
 * @file inet_address.cpp
 * @brief 网络地址类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:37:00
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/inet_address.h"

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



}   //kit_muduo