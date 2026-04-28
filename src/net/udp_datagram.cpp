/**
 * @file udp_datagram.cpp
 * @brief UDP报文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-03-27 15:23:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/udp_datagram.h"
#include "net/inet_address.h"
#include "net/socket.h"
#include "net/channel.h"
#include "net/event_loop.h"
#include "net/net_log.h"

#include <cstdint>
#include <exception>
#include <cstring>
#include <functional>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>


namespace kit_muduo {

UdpDatagram::UdpDatagram(EventLoop *base_loop, const std::string& name, int32_t sockfd)
    :base_loop_(base_loop)
    ,name_(name)
    ,socket_(std::make_unique<Socket>(sockfd, Socket::Type::UDP))
    ,channel_((base_loop != nullptr) ? std::make_unique<Channel>(base_loop, sockfd, false) : nullptr) // UDP是无连接的
    ,state_(kActive)
{
    // 可以选择不使用eventloop
    if(channel_)
    {
        channel_->setReadCallback([this](auto &&arg1) {
            handleRead(std::forward<decltype(arg1)>(arg1)); 
        });
        // 绑定地址后在开始监听

        channel_->setWriteCallback([this](){
            handleWrite();
        });
    
        channel_->setErrorCallback([this](){
            handleError();
        });


    }
  
    CONN_F_DEBUG("UdpDatagram create: name[%s], fd[%d]\n", name.c_str(), sockfd);
}

UdpDatagram::~UdpDatagram() = default;

void UdpDatagram::enableReading()
{
    if(channel_)
    {
        channel_->enableReading();
    }

}

bool UdpDatagram::bind(const InetAddress &local_addr) 
{ 
    if(!socket_->isBind() && !socket_->bindAddress(local_addr))
    {
        return false;
    }
    enableReading();
    
    return true;
}

ssize_t UdpDatagram::sendTo(const std::vector<uint8_t> &message, const InetAddress &peer_addr)
{
    return sendTo(message.data(), message.size(), peer_addr);
}

ssize_t UdpDatagram::sendTo(const void* buf, size_t len, const InetAddress &peer_addr)
{
    ssize_t n = ::sendto(socket_->fd(), buf, len, 0, (sockaddr*)peer_addr.getSockAddr(),  static_cast<socklen_t>(sizeof(sockaddr_in)));
    if(n <= 0)
    {
        UDP_F_ERROR("sendTo error! name[%s], fd[%d], %s, %d:%s\n", name_.c_str(), socket_->fd(),  peer_addr.toIpPort().c_str(), errno, strerror(errno));
        // notifyError(errno, peer_addr);
        return -1;
    }
    return n;
}

void UdpDatagram::send(const void* buf, size_t len, const InetAddress &peer_addr)
{
    send(std::vector<uint8_t>((char*)buf, (char*)buf + len),  peer_addr);
}


void UdpDatagram::send(const std::vector<uint8_t> &message, const InetAddress &peer_addr)
{
    if(!channel_ || !base_loop_) 
    {
        UDP_F_WARN("now not async mode! send faild \n");
        return;
    }

    if(kActive == state_)
    {
        if(base_loop_->isInLoopThread())
        {
            sendInLoop(message, peer_addr);
        }
        else
        {
            UDP_F_DEBUG("UdpDatagram::send queue fd[%d][%s] \n", fd(), peer_addr.toIpPort().c_str());

            base_loop_->queueInLoop([this_ptr = shared_from_this(), message, peer_addr](){
                this_ptr->sendInLoop(message, peer_addr);
            });
                

        }
    }
    else
    {
        UDP_F_INFO("fd[%d][%s] has closed! \n", fd(), peer_addr.toIpPort().c_str());
    }

}





ssize_t UdpDatagram::recvFrom(std::vector<uint8_t> &message, InetAddress &peer_addr)
{
    if(message.size() < kMTU)
    {
        message.resize(kMTU);
    }

    ssize_t recv_size = recvFrom(message.data(), message.size(), peer_addr);
    if(recv_size >= 0)
    {
        message.resize(static_cast<size_t>(recv_size));
    }

    return recv_size;
}


void UdpDatagram::removeInLoop()
{
    state_ = kClosing;
    if(channel_)
    {
        if(!channel_->isWriting())
        {
            // 取消事件监听
            channel_->disableAll();
            channel_->remove();
            state_ = kClosed;
        }

    }
}

void UdpDatagram::remove()
{
    if(kActive == state_)
    {
        // 处理同步模式
        if(nullptr == base_loop_)
        {
            state_ = kClosed;
            return;
        }
        
        
        if(base_loop_->isInLoopThread())
        {
            removeInLoop();
        }
        else
        {
            UDP_F_DEBUG("UdpDatagram::remove queue fd[%d]\n", fd());

            base_loop_->queueInLoop([this_ptr = shared_from_this()](){
                this_ptr->removeInLoop();
            });
        }
    }
    else
    {
        UDP_F_ERROR("fd[%d] has closed! \n", fd());
    }
}


ssize_t UdpDatagram::recvFrom(void *buf, size_t buf_len, InetAddress &peer_addr)
{
    struct sockaddr_in addr = {0};
    socklen_t addr_len = sizeof(addr);

    ssize_t n = ::recvfrom(socket_->fd(), buf, buf_len, 0, (sockaddr*)&addr, &addr_len);
    if(n < 0)
    {
        UDP_F_ERROR("recvfrom error! %s \n", strerror(errno));
        return -1;
    }
    peer_addr.setSockAddr(addr);
    return n;
}

void UdpDatagram::handleRead(TimeStamp receiveTime)
{
    std::vector<uint8_t> message;
    InetAddress peer_addr;
    ssize_t recv_size = recvFrom(message, peer_addr);
    if(recv_size <= 0)
    {
        UDP_F_ERROR("fd[%d] handleRead error! peer[%s]\n", fd(), peer_addr.toIpPort().c_str());
        return;
    }

    if(message_callback_)
    {
        try
        {
            message_callback_(message, peer_addr, receiveTime);
        }
        catch(const std::exception &e)
        {
            UDP_F_WARN("fd[%d] peer[%s] message callback exception: \n",
                fd(), peer_addr.toIpPort().c_str(), e.what());
        }
    }
}
void UdpDatagram::handleWrite()
{
    int fd = socket_->fd();
    if(!channel_->isWriting())
    {
        UDP_F_WARN("channel fd[%d] closed, no more write! \n", fd);
        return;
    }

    while(!pending_datagrams_.empty())
    {
        const PendingDatagram &datagram = pending_datagrams_.front();
        ssize_t n = ::sendto(fd,
            datagram.payload.data(),
            datagram.payload.size(),
            0,
            (const sockaddr*)(datagram.peer_addr.getSockAddr()),
            sizeof(sockaddr_in));
        if(n < 0)
        {
            UDP_F_ERROR("fd[%d] handleWrite error! %d:%s \n", fd, errno, strerror(errno));
            // 已写不进去 直接返回等待下一次事件回调
            if(EAGAIN == errno || EWOULDBLOCK == errno)
            {
                return;
            }
            notifyError(errno, datagram.peer_addr);
            pending_datagrams_.pop_front();
            continue;
        }

        if(static_cast<size_t>(n) != datagram.payload.size())
        {
            UDP_F_ERROR("fd[%d] udp short write! expect[%zu], actual[%zd], peer[%s]\n",
                fd,
                datagram.payload.size(),
                n,
                datagram.peer_addr.toIpPort().c_str());
            notifyError(EIO, datagram.peer_addr);
            pending_datagrams_.pop_front();
            return;
        }

        pending_datagrams_.pop_front();
    }

    // 把UdpDatagram上的遗留数据全部写完
    channel_->disableWriting();
    if(write_complete_callback_)
    {
        base_loop_->queueInLoop(write_complete_callback_);
    }

    if(kClosing == state_)
    {
        removeInLoop();
    }

}

void UdpDatagram::handleError()
{
    int32_t opt;
    int32_t err;
    socklen_t len = sizeof(opt);
    if(::getsockopt(socket_->fd(), SOL_SOCKET, SO_ERROR, &opt, &len) < 0)
    {
        err = errno;
    }
    else
    {
        err = opt;
    }
    UDP_F_WARN("UdpDatagram error: name[%s], fd[%d], %d:%s\n", name_.c_str(), socket_->fd(), err, strerror(err));
    
    notifyError(err, InetAddress());
}

void UdpDatagram::notifyError(int32_t err, const InetAddress &peer_addr)
{
    if(error_callback_)
    {
        error_callback_(err, peer_addr);
    }
}

void UdpDatagram::sendInLoop(const void* buf, size_t len, const InetAddress &peer_addr)
{

    sendInLoop(std::vector<uint8_t>((char*)buf, (char*)buf + len), peer_addr);
}

void UdpDatagram::sendInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr)
{
    int32_t fd = socket_->fd();

    if(kActive != state_)
    {
        UDP_F_ERROR("sendInLoop state error! fd[%d] name[%s], fd[%d], state[%d]\n", fd, name_.c_str(), state_.load());
        return;
    }


    // FIX: IO操作都需要放在loop的队列里来完成无锁操作，否则线程不安全

    // 最外层用户调的send 此时不应该在监听写事件，否则说明上一次都没发送完成
    if(!channel_->isWriting() && pending_datagrams_.empty())
    {
        UDP_F_DEBUG("UdpDatagram::sendInLoop write fd[%d][%s]\n", fd, name_.c_str());

        ssize_t n = ::sendto(fd,
            message.data(),
            message.size(),
            0,
            (const sockaddr*)(peer_addr.getSockAddr()),
            sizeof(sockaddr_in));
        if(n < 0)
        {
            UDP_F_ERROR("sendInLoop write error! name[%s], fd[%d], %s, %d:%s\n", name_.c_str(), fd,  peer_addr.toIpPort().c_str(), errno, strerror(errno));

            // 其他错误
            if(EAGAIN != errno
                && EWOULDBLOCK != errno)
            {
                notifyError(errno, peer_addr);
                return;
            }
        }
        else
        {
            if(static_cast<size_t>(n) != message.size())
            {
                UDP_F_ERROR("sendInLoop udp short write! name[%s], fd[%d], expect[%zu], actual[%zd], peer[%s]\n",
                    name_.c_str(),
                    fd,
                    message.size(),
                    n,
                    peer_addr.toIpPort().c_str());
                notifyError(EIO, peer_addr);
                return;
            }

            if(write_complete_callback_)
            {
                base_loop_->queueInLoop([this](){
                    write_complete_callback_();
                });
            }
            return;
        }
    }

    // UDP必须以“整条报文”为单位缓存，不能像TCP一样只缓存剩余字节。
    pending_datagrams_.push_back(PendingDatagram{message, peer_addr});

    UDP_F_INFO("UdpDatagram::sendInLoop queue datagram fd[%d][%s], pending[%zu]\n",
        fd, name_.c_str(), pending_datagrams_.size());
    if(!channel_->isWriting())
    {
        channel_->enableWriting();
    }
}



}
