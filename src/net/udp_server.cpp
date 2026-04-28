/**
 * @file udp_server.cpp
 * @brief UDP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-29 04:51:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/udp_server.h"
#include "base/thread_pool.h"
#include "net/net_log.h"
#include "net/socket.h"
#include "net/event_loop.h"

#include <fcntl.h>
#include <functional>
#include <memory>

namespace kit_muduo {

UdpServer::UdpServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
    :base_loop_(loop)
    ,name_(name)
    ,local_addr_(addr)
    ,acceptor_(std::make_unique
    <UdpDatagram>(loop, name, Socket::CreateUdpIpv4()))
    ,work_thread_pool_(std::make_unique<ThreadPool>())
    ,started_(0)
    ,message_callback_(nullptr)
{
    acceptor_->setMessageCallback(std::bind(&UdpServer::newDatagram, this,  std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3));

}

UdpServer::~UdpServer()
{

}

void UdpServer::start()
{
    // 乐观锁机制
    if(started_ > 0)
    {
        return;
    }
    ++started_;

    work_thread_pool_->start();

    base_loop_->runInLoop([this](){
        acceptor_->bind(local_addr_);    
    });
}

void UdpServer::newDatagram(const std::vector<uint8_t>& message, const InetAddress& peer_addr, TimeStamp time_stamp)
{
    UDP_F_INFO("newDatagram===> peer[%s], size[%zu]\n", peer_addr.toIpPort().c_str(), message.size());

    // 疑问：这里需要对多个报文进行分流操作

    // 做业务处理 这里是默认行为
    if(message_callback_)
    {
        message_callback_(message, peer_addr, time_stamp);
    }
}



}
