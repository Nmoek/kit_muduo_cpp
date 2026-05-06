/**
 * @file async_udp_datagram.h
 * @brief 异步UDP报文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_ASYNC_UDP_DATAGRAM_H__
#define __KIT_ASYNC_UDP_DATAGRAM_H__

#include "base/noncopyable.h"
#include "net/call_backs.h"
#include "net/udp_datagram.h"

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

namespace kit_muduo {

class Channel;
class EventLoop;

/**
 * @brief 异步 UDP 报文端点
 *
 * 该类必须由 std::shared_ptr 管理。调用 start() 后会把 Channel 注册到 EventLoop，
 * 销毁前应显式调用 close() 或 forceClose() 完成 Channel 注销。
 */
class AsyncUdpDatagram: Noncopyable, public std::enable_shared_from_this<AsyncUdpDatagram>
{
public:
    enum State {kNew, kActive, kClosing, kClosed};

    static AsyncUdpDatagramPtr Create(EventLoop *base_loop, const std::string& name, int32_t sockfd);

    ~AsyncUdpDatagram();
    
    EventLoop *getLoop() const { return base_loop_; }
    std::string name() const { return datagram_.name(); }
    int32_t fd() const { return datagram_.fd(); }

    void setMessageCallback(const UdpMessageCb &cb) { message_callback_ = std::move(cb); }

    void setWriteCompleteCallback(const UdpWriteCompleteCb &cb) { write_complete_callback_ = std::move(cb); }

    /**
     * @brief 设置UDP错误回调
     * @param[in] cb 错误回调，主要用于异步发送链路和底层事件错误，入参为错误码与对端地址
     */
    void setErrorCallback(const UdpErrorCb &cb) { error_callback_ = std::move(cb); }

    /**
     * @brief 绑定本地地址，不会启动读事件监听
     */
    bool bind(const InetAddress &local_addr);

    /**
     * @brief 注册 Channel 并启动读事件监听
     */
    void start();

    void send(const std::vector<uint8_t>& message, const InetAddress &peer_addr);

    void send(const void* buf, size_t len, const InetAddress &peer_addr);

    /**
     * @brief 平滑关闭。若还有待发送报文，等待写完后注销 Channel。
     */
    void close();

    /**
     * @brief 立即关闭，丢弃待发送报文并注销 Channel。
     */
    void forceClose();

public:
    static const int32_t kMTU = 1500;

private:
    AsyncUdpDatagram(EventLoop *base_loop, const std::string& name, int32_t sockfd);

    static void Destroy(AsyncUdpDatagram *datagram);

    void destroyInLoop();

    void startInLoop();
    void handleRead(TimeStamp receiveTime);
    void handleWrite();
    void handleError();
    /**
     * @brief 通知上层发生了UDP错误
     * @param[in] err 错误码
     * @param[in] peer_addr 对端地址，若来自底层socket事件则可能为默认地址
     */
    void notifyError(int32_t err, const InetAddress &peer_addr);

    ssize_t sendDatagramInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr);

    void sendInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr);

    void closeInLoop();

    void forceCloseInLoop();

    void removeChannelInLoop();

    void queueWriteCompleteCallback();

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
    UdpDatagram datagram_;
    std::unique_ptr<Channel> channel_;
    std::atomic_int state_;
    bool channel_removed_;

    UdpMessageCb message_callback_;
    UdpWriteCompleteCb write_complete_callback_;
    UdpErrorCb error_callback_;

    /// @brief 待发送报文队列，保持UDP报文边界不被破坏
    std::deque<PendingDatagram> pending_datagrams_;
};

}

#endif
