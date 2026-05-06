/**
 * @file udp_datagram.h
 * @brief UDP报文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-27 14:54:24
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_UDP_DATEGRAM_H__
#define __KIT_UDP_DATEGRAM_H__


#include "base/noncopyable.h"
#include "net/inet_address.h"
#include "net/socket.h"
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>


namespace kit_muduo {

class InetAddress;
// TODO 做成UDP套接字池

/**
 * @brief 同步 UDP 报文端点
 *
 * 该类只封装 fd、bind、sendto、recvfrom，不持有 Channel，也不依赖 EventLoop。
 * 异步事件监听和发送队列由 AsyncUdpDatagram 负责。
 */
class UdpDatagram: Noncopyable
{
public:
    UdpDatagram(const std::string& name, int32_t sockfd);

    ~UdpDatagram();
    
    std::string name() const { return name_; }
    int32_t fd() const { return socket_->fd(); }

    bool bind(const InetAddress &local_addr);

    ssize_t sendTo(const std::vector<uint8_t> &message, const InetAddress &peer_addr);

    ssize_t sendTo(const void* buf, size_t len, const InetAddress &peer_addr);

    ssize_t recvFrom(std::vector<uint8_t> &message, InetAddress &peer_addr);

    ssize_t recvFrom(void *buf, size_t buf_len, InetAddress &peer_addr);

public:
    static const int32_t kMTU = 1500;

private:
    std::string name_;
    std::unique_ptr<Socket> socket_;
};
}
#endif
