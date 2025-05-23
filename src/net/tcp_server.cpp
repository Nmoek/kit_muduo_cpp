/**
 * @file tcp_server.cpp
 * @brief TcpServer对外调用类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-23 21:48:35
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/tcp_server.h"
#include "net/net_log.h"
#include "net/inet_address.h"
#include "net/acceptor.h"
#include "base/event_loop_thread.h"
#include "base/event_loop_thread_pool.h"


namespace kit_muduo {

static EventLoop* CheckNullLoop(EventLoop *p)
{
    if(!p)
    {
        TCP_F_FATAL("loop is null!\n");
        abort();
    }
    return p;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, Option option)
    :_baseLoop(CheckNullLoop(loop))
    ,_ipPort(addr.toIpPort())
    ,_name(name)
    ,_acceptor(new Acceptor(loop, addr, option == KReusetPort))
    ,_threadPool(std::make_shared<EventLoopThreadPool>(loop, name))
    ,_started(0)
    ,_connectionCallback(nullptr)
    ,_messageCallback(nullptr)
    ,_writeCompleteCallback(nullptr)
    ,_threadInitCallback(nullptr)
    ,_nextConnId(1)
{
    /* 关键:
        1. newConnections中获取子事件循环指针loop*(sub Reactor)
        2. 此时 main thread在操作 sub thread上的loop*
        3.runInLoop(cb) -----> cb将被放入到sub thread的队列中
    */
    _acceptor->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    for(auto &it : _connections)
    {
        // 把容器中的智能指针 ===转移==> 局部智能指针，保证一定能释放
        TcpConnectionPtr conn(it.second);
        it.second.reset();
        // 析构时再关闭一下 防止套接字泄漏
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
}


void TcpServer::setThreadNum(int32_t nums)
{
    _threadPool->setThreadNum(nums);
}


void TcpServer::start()
{
    // 乐观锁机制
    if(_started > 0)
        return;
    ++_started;

    _baseLoop->runInLoop([this](){
        _acceptor->listen();
    });
}

void TcpServer::newConnection(int32_t sockfd, const InetAddress& peerAddr)
{
    std::string conn_name = _name;
    conn_name += "-";
    conn_name += peerAddr.toIpPort();
    conn_name += "#";
    conn_name += std::to_string(_nextConnId);
    ++_nextConnId;
    TCP_F_INFO("==> new conn: fd[%d], name[%s] from %s \n", sockfd, peerAddr.toIpPort().c_str(), conn_name.c_str());\

    auto local_addr = InetAddress::GetLocalAddr(sockfd);

    EventLoop *sub_loop = _threadPool->getNextLoop();

    auto connPtr = std::make_shared<TcpConnection>(sub_loop, conn_name, sockfd, peerAddr, local_addr);

    connPtr->setConnectionCallback(_connectionCallback);
    connPtr->setWriteCompleteCallback(_writeCompleteCallback);
    connPtr->setMessageCallback(_messageCallback);
    connPtr->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    _connections[conn_name] = connPtr;

    // 直接调用 TcoConnection 连接建立函数
    sub_loop->runInLoop(std::bind(&TcpConnection::connectEstablished, connPtr));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    _baseLoop->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    TCP_F_INFO("TcpServer::removeConnectionInLoop [%s] ==> [%s] \n", _name.c_str(), conn->name().c_str());

    EventLoop *_subLoop = conn->getLoop();
    _connections.erase(conn->name());
    _subLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));

}

}   //kit_muduo