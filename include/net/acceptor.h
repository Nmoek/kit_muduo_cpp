/**
 * @file acceptor.h
 * @brief 服务端监听器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 00:48:05
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_ACCEPTOR_H__
#define __KIT_ACCEPTOR_H__

#include "base/noncopyable.h"
#include "net/socket.h"
#include "net/event_loop.h"
#include "net/channel.h"

namespace kit_muduo {

class InetAddress;

class Acceptor: Noncopyable
{
public:
    using NewConnectionCb = std::function<void(int32_t, const InetAddress&)>;

    Acceptor(EventLoop *loop, const InetAddress &addr, bool reuseport = true);

    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCb &cb) { _newConnectionCallback = std::move(cb); }

    bool listening() const { return _listening; }

    void listen();

private:
    void handleRead();

private:
    EventLoop *_loop;
    Socket _acceptSocket;
    Channel _acceptChannel;
    NewConnectionCb _newConnectionCallback;
    bool _listening;
};


}   //kit_muduo

#endif