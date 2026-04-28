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

static inline EventLoop* CheckNullLoop(EventLoop *p)
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
    ,_acceptor(std::make_unique<Acceptor>(loop, addr, option == KReusePort))
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
    ConnectMap tmpMap;
    {
    std::lock_guard<std::mutex> lock(_connectMapMtx);
    tmpMap.swap(_connections);
    }

    for(auto &it : tmpMap)
    {
        // 把容器中的智能指针 ===转移==> 局部智能指针，保证一定能释放
        TcpConnectionPtr conn = it.second;
        it.second.reset();
        TCP_F_DEBUG("~TcpServer::connectDestroyed fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());
        // 析构时再关闭一下 防止套接字泄漏
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }

    

    TCP_DEBUG() << "TcpServer::~TcpServer()" << std::endl;
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

    _threadPool->start(_threadInitCallback);

    _baseLoop->runInLoop([this](){
        _acceptor->listen();
    });
}

void TcpServer::addConnection(const std::string &name, TcpConnectionPtr conn)
{
    std::lock_guard<std::mutex> lock(_connectMapMtx);
    _connections[name] = conn;
}


TcpConnectionPtr TcpServer::getConnection(const std::string &name)
{
    std::lock_guard<std::mutex> lock(_connectMapMtx);
    if(_connections.empty())
        return nullptr;
    auto it = _connections.find(name);
    return it == _connections.end() ? nullptr : it->second;
}

void TcpServer::delConnection(const std::string &name)
{
    std::lock_guard<std::mutex> lock(_connectMapMtx);
    auto it = _connections.find(name);
    if(it != _connections.end())
        _connections.erase(it);
}


void TcpServer::newConnection(int32_t sockfd, const InetAddress& peerAddr)
{
    std::string conn_name = _name;
    conn_name += "-";
    conn_name += peerAddr.toIpPort();
    conn_name += "#";
    conn_name += std::to_string(_nextConnId.load());
    ++_nextConnId;
    TCP_F_INFO("==> new conn: fd[%d], name[%s] from %s \n", sockfd,  conn_name.c_str(), peerAddr.toIpPort().c_str());\

    auto local_addr = InetAddress::GetLocalAddr(sockfd);

    EventLoop *sub_loop = _threadPool->getNextLoop();

    auto connPtr = std::make_shared<TcpConnection>(sub_loop, conn_name, sockfd, peerAddr, local_addr);

    connPtr->setConnectionCallback(_connectionCallback);
    connPtr->setWriteCompleteCallback(_writeCompleteCallback);
    connPtr->setMessageCallback(_messageCallback);
    connPtr->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 此时TcpConnection ==> state=1 Connecting

    addConnection(conn_name, connPtr);

    // 设置当前连接状态+触发用户回调
    // 1. 这样写避免 TcpConnection::getChannel这种接口出现，借助std::bind绑定器也能实现执行成员函数效果
    // 2. 延迟执行
    
    sub_loop->runInLoop(std::bind(&TcpConnection::connectEstablished, connPtr));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
    TCP_F_INFO("TcpConnection::closeCb:: removeConnection queue fd[%d][%s] \n", conn->fd(), conn->name().c_str());

    _baseLoop->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    TCP_F_INFO("TcpServer::removeConnectionInLoop: fd[%d][%s] \n", conn->fd(), conn->name().c_str());

    EventLoop *_subLoop = conn->getLoop();

    delConnection(conn->name());

    _subLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));

}

}   //kit_muduo