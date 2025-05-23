/**
 * @file inet_address.h
 * @brief 网络地址类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 22:32:02
 * @copyright Copyright (c) 2025 Kewin Li
 */

#ifndef __KIT_INET_ADDRESS_H__
#define __KIT_INET_ADDRESS_H__

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <string>

namespace kit_muduo {

class InetAddress
{
public:
    /**
     * @brief 默认构造
     */
    InetAddress() = default;
    /**
     * @brief 普通构造
     * @param[in] port
     * @param[in] ip
     */
    explicit InetAddress(uint16_t port, std::string ip = "127.0.0.1");

    /**
     * @brief 普通构造
     * @param[in] addr
     */
    explicit InetAddress(const struct sockaddr_in &addr);

    /**
     * @brief 获取端口号
     * @return uint16_t
     */
    uint16_t toPort() const;

    /**
     * @brief 获取ip
     * @return std::string
     */
    std::string toIp() const;

    /**
     * @brief 获取ip端口 ip:port
     * @return std::string
     */
    std::string toIpPort() const;

    void setSockAddr(const struct sockaddr_in& addr) { _addr = addr; }

    const struct sockaddr_in* getSockAddr() const { return &_addr; }

public:
    static InetAddress GetLocalAddr(int32_t fd);

private:
    struct sockaddr_in _addr;
};


}   // kit_muduo
#endif