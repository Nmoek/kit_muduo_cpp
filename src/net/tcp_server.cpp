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

#include <algorithm>
#include <cassert>
#include <future>
#include <stdexcept>
#include <unistd.h>


namespace kit_muduo {

namespace {

struct StopState
{
    std::atomic_size_t pending{0};
    kit_muduo::TcpServer::StopCb done;

    explicit StopState(kit_muduo::TcpServer::StopCb done)
        :done(std::move(done))
    {

    }

    void finishOne()
    {
        if(pending.fetch_sub(1) == 1)
        {
            if(done)
            {
                done();
            }
        }
    }

};

}

static inline EventLoop* CheckNullLoop(EventLoop *p)
{
    if(!p)
    {
        TCP_F_FATAL("loop is null!\n");
        throw std::invalid_argument("loop* is null");
    }
    return p;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, Option option)
    :_baseLoop(CheckNullLoop(loop))
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
    // (暂时不能这么写)强断言 倒逼上层调stop
    // assert(_started.load() == false);
    stop();
    TCP_DEBUG() << "TcpServer::~TcpServer()" << std::endl;
}


void TcpServer::setThreadNum(int32_t nums)
{
    _threadPool->setThreadNum(nums);
}


void TcpServer::start()
{
    // CAS
    bool expected = false;
    if(!_started.compare_exchange_strong(expected, true))
    {
        return;
    }


    _threadPool->start(_threadInitCallback);

    _baseLoop->runInLoop([this](){
        _acceptor->listen();
    });
}

void TcpServer::stop()
{
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();

    stopAsync([done](){
        done->set_value();
    });

    future.get();
}

void TcpServer::stopAsync(StopCb done)
{
    //CAS
    bool expected = true;
    if(!_started.compare_exchange_strong(expected, false))
    {
        // 多次stop也需要触发回调
        if(done)
        {
            done();
        }
        return;
    }

    ConnectMap tmpMap;
    {
        std::lock_guard<std::mutex> lock(_connectMapMtx);
        for(auto &it : _connections)
        {
            if(it.second)
            {
                it.second->setCloseCallback(CloseCb());
            }
        }
        tmpMap.swap(_connections);
    }

    auto stop_state = std::make_shared<StopState>(std::move(done));
    // 1个Acceptor + N个TcpConnection
    stop_state->pending.store(tmpMap.size() + 1);

    _baseLoop->runInLoop([this, stop_state](){
        _acceptor->stop();
        stop_state->finishOne();
    });

    for(auto &it : tmpMap)
    {
        // 把容器中的智能指针 ===转移==> 局部智能指针，保证一定能释放
        TcpConnectionPtr conn = it.second;
        it.second.reset();

        if(!conn)
        {
            stop_state->finishOne();
            continue;
        }

        EventLoop *sub_loop = conn->getLoop();
        if(!sub_loop)
        {
            TCP_F_ERROR("TcpServer::stopAsync conn loop is null, name[%s]\n", conn->name().c_str());
            stop_state->finishOne();
            continue;
        }

        sub_loop->runInLoop([conn, stop_state](){
            TCP_F_DEBUG("~TcpServer::connectDestroyed fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());

            conn->connectDestroyed();
            stop_state->finishOne();
        });
    }

}

const InetAddress& TcpServer::getBindAddr() const 
{ 
    return _acceptor->getBindAddr(); 
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
    if(_started <= 0)
    {
        ::close(sockfd);
        TCP_F_INFO("tcp server is stopping.. fd[%d][%s] refuse\n", sockfd, peerAddr.toIpPort().c_str());
        return;
    }
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

    {
        std::lock_guard<std::mutex> lock(_connectMapMtx);
        auto it = _connections.find(conn->name());
        if(it == _connections.end() || it->second != conn)
        {
            TCP_F_INFO("TcpServer::removeConnectionInLoop skip stale conn: fd[%d][%s] \n",
                       conn->fd(), conn->name().c_str());
            return;
        }
        _connections.erase(it);
    }

    _subLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));

}

}   //kit_muduo
