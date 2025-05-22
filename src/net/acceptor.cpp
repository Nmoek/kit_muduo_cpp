/**
 * @file acceptor.cpp
 * @brief 服务端监听器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 00:48:27
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/acceptor.h"
#include "net/socket.h"
#include "net/inet_address.h"

#include <unistd.h>

namespace kit_muduo {

Acceptor::Acceptor(EventLoop *loop, const InetAddress &addr, bool reuseport)
    :_loop(loop)
    ,_acceptSocket(Socket::CreateIpv4(true))
    ,_acceptChannel(loop, _acceptSocket.fd())
    ,_newConnectionCallback(nullptr)
    ,_listening(false)
{
    _acceptSocket.setReuseAddr(reuseport);
    _acceptSocket.setReusePort(reuseport);
    _acceptSocket.setTcpNoDelay(true);
    _acceptSocket.bindAddress(addr);

    _acceptChannel.setReadCallback(std::bind(&Acceptor::handleRead, this));

}

Acceptor::~Acceptor()
{
    _acceptChannel.disableAll();
    _acceptChannel.remove();

}

void Acceptor::listen()
{
    _listening = true;
    _acceptSocket.listen();
    _acceptChannel.enableReading();

}

void Acceptor::handleRead()
{
    InetAddress peer_addr;
    int32_t connfd = _acceptSocket.accept(&peer_addr);
    if(connfd < 0)
        return;

    // 轮询找到合法的EventLoop* 将当前新连接的fd分发
    if(_newConnectionCallback)
        _newConnectionCallback(connfd, peer_addr);
    else
        ::close(connfd);


}

}   //kit_muduo
