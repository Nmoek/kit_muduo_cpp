/**
 * @file tcp_connection.h
 * @brief tcp连接
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 21:26:40
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_TCP_CONNECTION_H__
#define __KIT_TCP_CONNECTION_H__

#include "base/noncopyable.h"
#include "net/call_backs.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "net/inet_address.h"

#include <memory>
#include <string>
#include <atomic>

namespace kit_muduo {

class EventLoop;
class Socket;
class Channel;
class InetAddress;
class Buffer;

/*
    TcpServer ==> 新用户连接 ==> 获取到connFd
    ==> 封装Channel ==> EventLoop ==>  Poller ==> 调用Channel回调
*/
class TcpConnection: Noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop, const std::string &name, int32_t sockfd, const InetAddress &peerAddr, const InetAddress &localAddr);

    ~TcpConnection();

    EventLoop *getLoop() const { return _subLoop; }
    std::string name() const { return _name; }

    bool connected() const { return _state == kConnected; }

    void setConnectionCallback(const ConnectionCb &cb) { _connectionCallback = std::move(cb); }

    void setMessageCallback(const MessageCb &cb) { _messageCallback = std::move(cb); }

    void setWriteCompleteCallback(const WriteCompleteCb &cb) { _writeCompleteCallback = std::move(cb); }

    void setCloseCallback(const CloseCb &cb) { _closeCallback = std::move(cb); }


    void setHighWaterMarkCallback(const HighWaterMarkCb &cb) { _highWaterMarkCallback = std::move(cb); }

    void send(const std::string& buf);

    void shutdown();

    void connectEstablished();
    void connectDestroyed();

    InetAddress peerAddr() const { return _peerAddr; }

private:
    void handleRead(TimeStamp receiveTime);
    void handleWrite();
    void handleError();
    void handleClose();

    void sendInLoop(const void* message, size_t len);

    void shutdownInLoop();



private:
    enum State {kDisconnected, kConnecting, kConnected, kDisconnecting};

    /// @brief 一定是子事件循环
    EventLoop *_subLoop;
    std::string _name;
    std::atomic_int _state;
    bool _reading;

    std::unique_ptr<Socket> _socket;
    std::unique_ptr<Channel> _channel;

    const InetAddress _peerAddr;
    const InetAddress _localAddr;

    ConnectionCb _connectionCallback;
    MessageCb _messageCallback;
    WriteCompleteCb _writeCompleteCallback;
    CloseCb _closeCallback;

    size_t _highWaterMark;
    HighWaterMarkCb _highWaterMarkCallback;

    Buffer _inputBuffer;
    Buffer _outputBuffer;

};


}   // kit_muduo
#endif