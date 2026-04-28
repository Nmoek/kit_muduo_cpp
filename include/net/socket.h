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
    enum Type {
        TCP = 1,
        UDP = 2,
    };

    explicit Socket(int32_t fd, Socket::Type type = TCP);
    ~Socket();

    int32_t fd() const { return sockfd_; }
    int32_t get();
    bool bindAddress(const InetAddress &addr);
    bool isBind() const { return is_bind_; }
    void listen();
    int32_t accept(InetAddress *peerAddr);

    void shutdownWrite();

    void setTcpNoDelay(bool on);
    void setKeepAlive(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);

public:
    static int32_t CreateTcpIpv4(bool nonblock = true);
    static int32_t CreateUdpIpv4(bool nonblock = true);
private:
    int32_t sockfd_;
    Type type_;
    bool is_bind_;
};

}   //kit_muduo


#endif