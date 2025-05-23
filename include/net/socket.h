/**
 * @file socket.h
 * @brief socket封装
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 23:56:28
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_SOCKET_H__
#define __KIT_SOCKET_H__

#include "base/noncopyable.h"
#include <bits/stdint-intn.h>

namespace kit_muduo {

class InetAddress;

class Socket: Noncopyable
{
public:
    explicit Socket(int32_t fd);
    ~Socket();

    int32_t fd() const { return _sockfd; }

    void bindAddress(const InetAddress &addr);
    void listen();
    int32_t accept(InetAddress *peerAddr);

    void shutdownWrite();

    void setTcpNoDelay(bool on);
    void setKeepAlive(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);

public:
    static int32_t CreateIpv4(bool nonblock = true);
private:
    int32_t _sockfd;
};

}   //kit_muduo


#endif