/**
 * @file tcp_server.h
 * @brief TcpServer对外调用类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 01:45:14
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_TCP_SERVER_H__
#define __KIT_TCP_SERVER_H__

#include "base/noncopyable.h"
#include "net/call_backs.h"
#include "net/tcp_connection.h"

#include <functional>
#include <memory>
#include <atomic>
#include <string>

namespace kit_muduo {

class EventLoop;
class Acceptor;
class EventLoopThreadPool;
class InetAddress;

class TcpServer: Noncopyable
{
public:
    using ThreadInitCb = std::function<void(EventLoop*)>;
    enum Option
    {
        kNoRusePort,
        KReusetPort,
    };

    TcpServer(EventLoop *loop, const InetAddress &addr, const std::string &name = "", Option option = kNoRusePort);

    ~TcpServer();

    void setConnectionCallback(const ConnectionCb &cb) { _connectionCallback = std::move(cb); }

    void setMessageCallback(const MessageCb &cb) { _messageCallback = std::move(cb); }

    void setWriteCompleteCallback(const WriteCompleteCb &cb) { _writeCompleteCallback = std::move(cb); }

    void setThreadInitCallback(const ThreadInitCb &cb ) { _threadInitCallback = std::move(cb); }

    /**
     * @brief 设置线程池(子事件循环)个数
     * @param[in] nums
     */
    void setThreadNum(int32_t nums);

    /**
     * @brief 服务器开启监听
     */
    void start();

    /**
     * @brief 获取事件循环句柄
     * @return EventLoop*
     */
    EventLoop *getLoop() const {return _baseLoop; }

private:
    void newConnection(int32_t sockfd, const InetAddress& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

private:
    using ConnectMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *_baseLoop;
    std::string _ipPort;
    std::string _name;
    std::unique_ptr<Acceptor> _acceptor;
    std::shared_ptr<EventLoopThreadPool> _threadPool;
    std::atomic_int _started;

    ConnectionCb _connectionCallback;
    MessageCb _messageCallback;
    WriteCompleteCb _writeCompleteCallback;
    ThreadInitCb _threadInitCallback;

    int32_t _nextConnId;
    ConnectMap _connections;
};


}   // kit_muduo
#endif