/**
 * @file async_udp_datagram.cpp
 * @brief 异步UDP报文
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/async_udp_datagram.h"
#include "net/channel.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include "net/net_log.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <sys/socket.h>
#include <vector>

namespace kit_muduo {

AsyncUdpDatagramPtr AsyncUdpDatagram::Create(EventLoop *base_loop,
    const std::string& name,
    int32_t sockfd)
{
    if(nullptr == base_loop)
    {
        UDP_F_ERROR("AsyncUdpDatagram::Create failed, loop is null! name[%s], fd[%d]\n",
            name.c_str(),
            sockfd);
        return nullptr;
    }
    
    return AsyncUdpDatagramPtr(new AsyncUdpDatagram(base_loop, name, sockfd), &AsyncUdpDatagram::Destroy);
}

AsyncUdpDatagram::AsyncUdpDatagram(EventLoop *base_loop, const std::string& name, int32_t sockfd)
    :base_loop_(base_loop)
    ,datagram_(name, sockfd)
    ,channel_(std::make_unique<Channel>(base_loop, sockfd, false))
    ,state_(kNew)
    ,channel_removed_(false)
{
    channel_->setReadCallback([this](TimeStamp receive_time) {
        handleRead(receive_time);
    });
    channel_->setWriteCallback([this]() {
        handleWrite();
    });
    channel_->setErrorCallback([this]() {
        handleError();
    });

    CONN_F_DEBUG("AsyncUdpDatagram create: name[%s], fd[%d]\n", name.c_str(), sockfd);
}

AsyncUdpDatagram::~AsyncUdpDatagram()
{
    if(kClosed != state_)
    {
        UDP_F_WARN("~AsyncUdpDatagram without close: name[%s], fd[%d], state[%d]\n",
            name().c_str(),
            fd(),
            state_.load());
    }
}

void AsyncUdpDatagram::Destroy(AsyncUdpDatagram *datagram)
{
    if(nullptr == datagram)
    {
        return;
    }

    EventLoop *loop = datagram->base_loop_;
    if(nullptr == loop
        || loop->isInLoopThread()
        || kNew == datagram->state_
        || kClosed == datagram->state_)
    {
        datagram->destroyInLoop();
        return;
    }

    loop->queueInLoop([datagram]() {
        datagram->destroyInLoop();
    });
}

void AsyncUdpDatagram::destroyInLoop()
{
    forceCloseInLoop();
    delete this;
}

bool AsyncUdpDatagram::bind(const InetAddress &local_addr)
{
    return datagram_.bind(local_addr);
}

void AsyncUdpDatagram::start()
{
    if(kNew != state_)
    {
        UDP_F_WARN("AsyncUdpDatagram::start ignored: name[%s], fd[%d], state[%d]\n",
            name().c_str(),
            fd(),
            state_.load());
        return;
    }

    if(base_loop_->isInLoopThread())
    {
        startInLoop();
    }
    else
    {
        base_loop_->queueInLoop([self = shared_from_this()]() {
            self->startInLoop();
        });
    }
}

void AsyncUdpDatagram::startInLoop()
{
    if(kNew != state_)
    {
        return;
    }

    channel_->tie(shared_from_this());
    state_ = kActive;
    channel_->enableReading();

    UDP_F_DEBUG("AsyncUdpDatagram::start fd[%d][%s]\n", fd(), name().c_str());
}

void AsyncUdpDatagram::send(const void* buf, size_t len, const InetAddress &peer_addr)
{
    std::vector<uint8_t> message;
    if(nullptr != buf && len > 0)
    {
        const auto *begin = static_cast<const uint8_t*>(buf);
        message.assign(begin, begin + len);
    }
    else if(nullptr == buf && len > 0)
    {
        UDP_F_ERROR("AsyncUdpDatagram::send invalid buffer: name[%s], fd[%d], len[%zu]\n",
            name().c_str(),
            fd(),
            len);
        return;
    }

    send(message, peer_addr);
}

void AsyncUdpDatagram::send(const std::vector<uint8_t> &message, const InetAddress &peer_addr)
{
    if(kActive != state_)
    {
        UDP_F_INFO("fd[%d][%s] not active, drop udp datagram to [%s], state[%d]\n",
            fd(),
            name().c_str(),
            peer_addr.toIpPort().c_str(),
            state_.load());
        return;
    }

    if(base_loop_->isInLoopThread())
    {
        sendInLoop(message, peer_addr);
    }
    else
    {
        UDP_F_DEBUG("AsyncUdpDatagram::send queue fd[%d][%s] \n", fd(), peer_addr.toIpPort().c_str());

        base_loop_->queueInLoop([self = shared_from_this(), message, peer_addr]() {
            self->sendInLoop(message, peer_addr);
        });
    }
}

void AsyncUdpDatagram::close()
{
    int old_state = state_.load();
    if(kClosed == old_state || kClosing == old_state)
    {
        return;
    }

    if(base_loop_->isInLoopThread())
    {
        closeInLoop();
    }
    else
    {
        base_loop_->queueInLoop([self = shared_from_this()]() {
            self->closeInLoop();
        });
    }
}

void AsyncUdpDatagram::forceClose()
{
    if(kClosed == state_)
    {
        return;
    }

    if(base_loop_->isInLoopThread())
    {
        forceCloseInLoop();
    }
    else
    {
        base_loop_->queueInLoop([self = shared_from_this()]() {
            self->forceCloseInLoop();
        });
    }
}

void AsyncUdpDatagram::closeInLoop()
{
    if(kClosed == state_ || kClosing == state_)
    {
        return;
    }

    state_ = kClosing;
    if(channel_ && !channel_removed_ && channel_->isWriting() && !pending_datagrams_.empty())
    {
        channel_->disableReading();
        return;
    }

    removeChannelInLoop();
}

void AsyncUdpDatagram::forceCloseInLoop()
{
    if(kClosed == state_)
    {
        return;
    }

    pending_datagrams_.clear();
    removeChannelInLoop();
}

void AsyncUdpDatagram::removeChannelInLoop()
{
    if(kClosed == state_)
    {
        return;
    }

    if(channel_ && !channel_removed_)
    {
        if(channel_->index() >= 0)
        {
            channel_->disableAll();
            channel_->remove();
        }
        channel_removed_ = true;
    }

    pending_datagrams_.clear();
    state_ = kClosed;
}

void AsyncUdpDatagram::handleRead(TimeStamp receiveTime)
{
    if(kActive != state_)
    {
        return;
    }

    std::vector<uint8_t> message;
    InetAddress peer_addr;
    ssize_t recv_size = datagram_.recvFrom(message, peer_addr);
    if(recv_size < 0)
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
            UDP_F_ERROR("!!![EXCEPTION]!!! fd[%d][%s] message callback exception: %s\n", fd(), peer_addr.toIpPort().c_str(), e.what());
        }
    }
}

void AsyncUdpDatagram::handleWrite()
{
    if(kActive != state_ && kClosing != state_)
    {
        return;
    }

    int cur_fd = fd();
    if(!channel_->isWriting())
    {
        UDP_F_WARN("channel fd[%d] closed, no more write! \n", cur_fd);
        return;
    }

    while(!pending_datagrams_.empty())
    {
        const PendingDatagram &datagram = pending_datagrams_.front();
        ssize_t n = sendDatagramInLoop(datagram.payload, datagram.peer_addr);
        if(n < 0)
        {
            if(EAGAIN == errno || EWOULDBLOCK == errno)
            {
                return;
            }

            UDP_F_ERROR("fd[%d] handleWrite error! peer[%s], %d:%s \n",
                cur_fd,
                datagram.peer_addr.toIpPort().c_str(),
                errno,
                strerror(errno));
            notifyError(errno, datagram.peer_addr);
            pending_datagrams_.pop_front();
            continue;
        }

        if(static_cast<size_t>(n) != datagram.payload.size())
        {
            UDP_F_ERROR("fd[%d] udp short write! expect[%zu], actual[%zd], peer[%s]\n",
                cur_fd,
                datagram.payload.size(),
                n,
                datagram.peer_addr.toIpPort().c_str());
            notifyError(EIO, datagram.peer_addr);
            pending_datagrams_.pop_front();
            continue;
        }

        pending_datagrams_.pop_front();
    }

    channel_->disableWriting();
    queueWriteCompleteCallback();

    if(kClosing == state_)
    {
        removeChannelInLoop();
    }
}

void AsyncUdpDatagram::handleError()
{
    int32_t opt = 0;
    int32_t err = 0;
    socklen_t len = sizeof(opt);
    if(::getsockopt(fd(), SOL_SOCKET, SO_ERROR, &opt, &len) < 0)
    {
        err = errno;
    }
    else
    {
        err = opt;
    }
    UDP_F_WARN("AsyncUdpDatagram error: name[%s], fd[%d], %d:%s\n",
        name().c_str(),
        fd(),
        err,
        strerror(err));
    
    notifyError(err, InetAddress());
}

void AsyncUdpDatagram::notifyError(int32_t err, const InetAddress &peer_addr)
{
    if(error_callback_)
    {
        try
        {
            error_callback_(err, peer_addr);
        }
        catch(const std::exception &e)
        {
            UDP_F_WARN("fd[%d] udp error callback exception: %s\n", fd(), e.what());
        }
    }
}

ssize_t AsyncUdpDatagram::sendDatagramInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr)
{
    return ::sendto(fd(),
        message.data(),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(peer_addr.getSockAddr()),
        static_cast<socklen_t>(sizeof(sockaddr_in)));
}

void AsyncUdpDatagram::sendInLoop(const std::vector<uint8_t>& message, const InetAddress &peer_addr)
{
    int32_t cur_fd = fd();

    if(kActive != state_)
    {
        UDP_F_ERROR("sendInLoop state error! name[%s], fd[%d], state[%d]\n",
            name().c_str(),
            cur_fd,
            state_.load());
        return;
    }

    if(!channel_->isWriting() && pending_datagrams_.empty())
    {
        UDP_F_DEBUG("AsyncUdpDatagram::sendInLoop write fd[%d][%s]\n", cur_fd, name().c_str());

        ssize_t n = sendDatagramInLoop(message, peer_addr);
        if(n < 0)
        {
            if(EAGAIN != errno && EWOULDBLOCK != errno)
            {
                UDP_F_ERROR("sendInLoop write error! name[%s], fd[%d], %s, %d:%s\n",
                    name().c_str(),
                    cur_fd,
                    peer_addr.toIpPort().c_str(),
                    errno,
                    strerror(errno));
                notifyError(errno, peer_addr);
                return;
            }
        }
        else
        {
            if(static_cast<size_t>(n) != message.size())
            {
                UDP_F_ERROR("sendInLoop udp short write! name[%s], fd[%d], expect[%zu], actual[%zd], peer[%s]\n",
                    name().c_str(),
                    cur_fd,
                    message.size(),
                    n,
                    peer_addr.toIpPort().c_str());
                notifyError(EIO, peer_addr);
                return;
            }

            queueWriteCompleteCallback();
            return;
        }
    }

    pending_datagrams_.push_back(PendingDatagram{message, peer_addr});

    UDP_F_INFO("AsyncUdpDatagram::sendInLoop queue datagram fd[%d][%s], pending[%zu]\n",
        cur_fd, name().c_str(), pending_datagrams_.size());
    if(!channel_->isWriting())
    {
        channel_->enableWriting();
    }
}

void AsyncUdpDatagram::queueWriteCompleteCallback()
{
    if(!write_complete_callback_)
    {
        return;
    }

    base_loop_->queueInLoop([weak_self = weak_from_this()]() {
        auto self = weak_self.lock();
        if(!self || kActive != self->state_)
        {
            return;
        }

        if(self->write_complete_callback_)
        {
            self->write_complete_callback_();
        }
    });
}

}
