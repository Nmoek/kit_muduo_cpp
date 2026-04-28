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
#include "net/call_backs.h"
#include "net/inet_address.h"
#include "net/socket.h"
#include <atomic>
#include <deque>
#include <memory>
#include <sys/types.h>
#include <vector>


namespace kit_muduo {

class EventLoop;
class Channel;
class InetAddress;
// TODO 做成UDP套接字池

class UdpDatagram: Noncopyable, public std::enable_shared_from_this<UdpDatagram>
{
public:
    enum State {kActive, kClosing, kClosed};

    UdpDatagram(EventLoop *base_loop, const std::string& name, int32_t sockfd);

    ~UdpDatagram();
    
    EventLoop *getLoop() const { return base_loop_; }
    std::string name() const { return name_; }
    int32_t fd() const { return socket_->fd(); }

    void setMessageCallback(const UdpMessageCb &cb) { message_callback_ = std::move(cb); }

    void setWriteCompleteCallback(const UdpWriteCompleteCb &cb) { write_complete_callback_ = std::move(cb); }

    /**
     * @brief 设置UDP错误回调
     * @param[in] cb 错误回调，主要用于异步发送链路和底层事件错误，入参为错误码与对端地址
     */
    void setErrorCallback(const UdpErrorCb &cb) { error_callback_ = std::move(cb); }

    /**
     * @brief 打开 UDP 读事件监听
     * @return bool 成功返回 true；同步模式或无 channel 时返回 false
     */
    void enableReading();

    bool bind(const InetAddress &local_addr);

    ssize_t sendTo(const std::vector<uint8_t> &message, const InetAddress &peer_addr);

    ssize_t sendTo(const void* buf, size_t len, const InetAddress &peer_addr);

    void send(const std::vector<uint8_t>& message, const InetAddress &peer_addr);

    void send(const void* buf, size_t len, const InetAddress &peer_addr);


    ssize_t recvFrom(std::vector<uint8_t> &message, InetAddress &peer_addr);

    ssize_t recvFrom(void *buf, size_t buf_len, InetAddress &peer_addr);

    void remove();

public:
    static const int32_t kMTU = 1500;

private:
    void handleRead(TimeStamp receiveTime);
    void handleWrite();
    void handleError();
    /**
     * @brief 通知上层发生了UDP错误
     * @param[in] err 错误码
     * @param[in] peer_addr 对端地址，若来自底层socket事件则可能为默认地址
     */
    void notifyError(int32_t err, const InetAddress &peer_addr);

    void sendInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr);

    void sendInLoop(const void* buf, size_t len, const InetAddress &peer_addr);

    void removeInLoop();

private:
    /**
     * @brief 待异步发送的单条UDP报文
     */
    struct PendingDatagram
    {
        std::vector<uint8_t> payload;
        InetAddress peer_addr;
    };

    EventLoop *base_loop_;
    std::string name_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;
    std::atomic_int state_;
    std::weak_ptr<void> tie_;

    UdpMessageCb message_callback_;
    UdpWriteCompleteCb write_complete_callback_;
    UdpErrorCb error_callback_;

    /// @brief 待发送报文队列，保持UDP报文边界不被破坏
    std::deque<PendingDatagram> pending_datagrams_;
};


}
#endif
