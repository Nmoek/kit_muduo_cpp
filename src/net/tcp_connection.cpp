/**
 * @file tcp_connection.cpp
 * @brief tcp连接
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-24 03:25:16
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/tcp_connection.h"
#include "net/socket.h"
#include "net/channel.h"
#include "net/net_log.h"
#include "net/event_loop.h"
#include <unistd.h>


namespace kit_muduo {

#define HIGH_WATER_MARK_MAX     (64*1024*1024) // 64M

TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int32_t sockfd, const InetAddress &peerAddr, const InetAddress &localAddr)
    :_subLoop(loop)
    ,_name(name)
    ,_reading(true)
    ,_socket(new Socket(sockfd))
    ,_peerAddr(peerAddr)
    ,_localAddr(localAddr)
    ,_highWaterMark(HIGH_WATER_MARK_MAX)
    ,_state(kConnecting)
    ,_channel(new Channel(loop, sockfd, true))
{
    _channel->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));

    _channel->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));

    _channel->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    _channel->setCloseCallback(std::bind(&TcpConnection::handleClose, this));

    _socket->setKeepAlive(true);

    CONN_F_DEBUG("TcpConnection create: name[%s], fd[%d], state[%d], %s\n", name.c_str(), sockfd, _state.load(), peerAddr.toIpPort().c_str());

}


TcpConnection::~TcpConnection()
{
    CONN_F_DEBUG("~TcpConnection: name[%s], fd[%d][%s], state[%d]\n", _name.c_str(), _socket->fd(), _peerAddr.toIpPort().c_str(), _state.load());
}

void TcpConnection::send(const std::string& buf)
{
    send(std::move(std::vector<char>(buf.begin(), buf.end())));
}

void TcpConnection::send(const std::vector<char>& buf)
{
    if(kConnected == _state)
    {
        if(_subLoop->isInLoopThread())
        {
            sendInLoop(buf.data(), buf.size());
        }
        else
        {

            TCP_F_DEBUG("TcpConnection::send queue fd[%d][%s] \n", fd(), _peerAddr.toIpPort().c_str());

            _subLoop->queueInLoop([buf, this_ptr = shared_from_this()](){
                this_ptr->sendInLoop(buf);
            });

        }
    }
    else
    {
        TCP_F_INFO("fd[%d][%s] has closed! \n", fd(), _peerAddr.toIpPort().c_str());
    }
}

void TcpConnection::send(const std::vector<uint8_t>& buf)
{
    if(kConnected == _state)
    {
        if(_subLoop->isInLoopThread())
        {
            sendInLoop(buf.data(), buf.size());
        }
        else
        {

            TCP_F_DEBUG("TcpConnection::send queue fd[%d][%s] \n", fd(), _peerAddr.toIpPort().c_str());

            _subLoop->queueInLoop([msg = std::move(buf), this_ptr = shared_from_this()](){
                this_ptr->sendInLoop(msg);
            });

        }
    }
    else
    {
        TCP_F_INFO("fd[%d][%s] has closed! \n", fd(), _peerAddr.toIpPort().c_str());
    }
}

void TcpConnection::shutdown()
{
    if(kConnected == _state)
    {

        if(_subLoop->isInLoopThread())
        {
            shutdownInLoop();
        }
        else
        {
            TCP_F_DEBUG("TcpConnection::shutdown queue fd[%d][%s] \n", fd(), _peerAddr.toIpPort().c_str());

            _subLoop->queueInLoop(std::bind(&TcpConnection::shutdownInLoop, shared_from_this()));
        }
    }
    else
    {
        TCP_F_ERROR("fd[%d][%s] has closed! \n", fd(), _peerAddr.toIpPort().c_str());
    }
}

void TcpConnection::connectEstablished()
{
    _channel->tie(shared_from_this());
    _channel->enableReading();
    establishTransport();
}

void TcpConnection::establishTransport()
{
    notifyConnected();
}

void TcpConnection::notifyConnected()
{
    _state = kConnected;
    connection_notified_ = true;
    if(_connectionCallback)
        _connectionCallback(shared_from_this());
}

void TcpConnection::notifyMessage(TimeStamp receive_time)
{
    if(_messageCallback)
        _messageCallback(shared_from_this(), &_inputBuffer, receive_time);
}

void TcpConnection::notifyWriteComplete()
{
    if(_writeCompleteCallback)
        _subLoop->queueInLoop(std::bind(_writeCompleteCallback, shared_from_this()));
}

void TcpConnection::connectDestroyed()
{
    if(channel_removed_)
        return;
    const bool notify = connection_notified_ && kDisconnected != _state;
    _state = kDisconnected;
    _channel->disableAll();
    closeTransport();
    if(notify && _connectionCallback)
        _connectionCallback(shared_from_this());
    // Channel 必须在所属 Loop 中移除，再释放连接对象。
    _channel->remove();
    channel_removed_ = true;
}

void TcpConnection::handleRead(TimeStamp receiveTime)
{
    int32_t saved_errno;
    int32_t fd = _socket->fd();
    ssize_t n = _inputBuffer.readFd(fd, &saved_errno);
    if(n < 0)
    {
        errno = saved_errno;
        CONN_F_ERROR("fd[%d] handleRead error! %d:%s \n", fd, errno, strerror(errno));
        return;
    }
    else if(0 == n)
    {
        CONN_F_DEBUG("read n == 0, fd[%d][%s]\n", fd, _name.c_str());
        handleClose();
        return;
    }

    // 用户传入的Message处理
    // 存在改进点：业务处理异步出Loop线程
    notifyMessage(receiveTime);
}

void TcpConnection::handleWrite()
{
    int fd = _socket->fd();
    if(_channel->isWriting())
    {
        int32_t saved_errno;
        ssize_t n = _outputBuffer.writeFd(fd, &saved_errno);
        if(n < 0)
        {
            errno = saved_errno;
            CONN_F_ERROR("fd[%d] handleRead error! %d:%s \n", fd, errno, strerror(errno));
            return;
        }

        _outputBuffer.reset(n);
        if(0 == _outputBuffer.readableBytes())
        {
            // 写完了 停止写入
            _channel->disableWriting();
            notifyWriteComplete();

            //这里的含义: 关闭时要注意 等待全部数据写完再关闭
            if(kDisconnecting == _state)
            {
                shutdownInLoop();
            }
        }
    }
    else
    {
        CONN_F_WARN("fd[%d] shutdown, no more write! \n", fd);
    }
}

void TcpConnection::handleError()
{
    int32_t opt;
    int32_t err;
    socklen_t len = sizeof(opt);
    if(::getsockopt(_socket->fd(), SOL_SOCKET, SO_ERROR, &opt, &len) < 0)
    {
        err = errno;
    }
    else
    {
        err = opt;
    }
    CONN_F_WARN("TcpConnection error: name[%s], fd[%d], state[%d], %s, %d:%s\n", _name.c_str(), _socket->fd(), _state.load(), _peerAddr.toIpPort().c_str(), err, strerror(err));
    onSocketError(err);
}

void TcpConnection::handleClose()
{
    if(kDisconnected == _state)
        return;
    CONN_F_INFO("TcpConnection close: name[%s], fd[%d], state[%d], %s\n", _name.c_str(), _socket->fd(), _state.load(), _peerAddr.toIpPort().c_str());

    _state = kDisconnected;
    _channel->disableAll(); // 这里只是删除了epoll里的监听
    closeTransport();

    // 注意 这里需要去触发一下用户传入的回调函数
    if(_connectionCallback && connection_notified_)
        _connectionCallback(shared_from_this());

    if(_closeCallback)
        _closeCallback(shared_from_this());
}

/*注意这里是用户调用send, 而非EventLoop事件触发进行send*/
// 多线程情况下这里不能使用指针
void TcpConnection::sendInLoop(const void* message, size_t len)
{
    int32_t fd = _socket->fd();
    ssize_t n = 0;
    ssize_t remain = len;

    if(kConnected != _state)
    {
        CONN_F_ERROR("sendInLoop state error! fd[%d][%s], fd[%d], state[%d]\n", fd, _name.c_str(), _state.load());
        return;
    }


    // 最外层用户调的send 此时不应该在监听写事件，否则说明上一次都没发送完成
    if(!_channel->isWriting() && 0 == _outputBuffer.readableBytes())
    {
        CONN_F_INFO("TcpConnection::sendInLoop write fd[%d][%s], state[%d]\n", fd, _name.c_str() ,_state.load());

        n = ::write(fd, message, len);
        if(n < 0)
        {
            n = 0;
            CONN_F_ERROR("sendInLoop write error! name[%s], fd[%d], %s, %d:%s\n", _name.c_str(), fd,  _peerAddr.toIpPort().c_str(), errno, strerror(errno));

            // 其他错误
            if(EAGAIN != errno
                && EWOULDBLOCK != errno)
            {
                return;
            }

        }
        else
        {
            remain = len - n;
            // 一次性全部写完的情况
            if(0 == remain && _writeCompleteCallback)
            {
                notifyWriteComplete();

                return;
            }
        }
    }

    //情况1: 内核缓冲区满了
    //情况2: 没报错，但是也没全部写完
    if(remain > 0)
    {
        CONN_F_INFO("TcpConnection::sendInLoop remain[%d] fd[%d][%s], state[%d]\n", remain, fd, _name.c_str() ,_state.load());

        _outputBuffer.ensureWritableBytes(remain);
        _outputBuffer.append((char*)message + n, remain);
        if(!_channel->isWriting())
            _channel->enableWriting();
    }
}

void TcpConnection::sendInLoop(const std::string message)
{
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const std::vector<char> message)
{
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const std::vector<uint8_t> message)
{
    sendInLoop(message.data(), message.size());
}

void TcpConnection::shutdownInLoop()
{
    if(kDisconnected == _state)
        return;
    _state = kDisconnecting;
    if(!_channel->isWriting())
    {
        TCP_F_DEBUG("TcpConnection::shutdownInLoop fd[%d][%s] \n", fd(), _peerAddr.toIpPort().c_str());
        // 触发EPOLL_HUB
        // 最终还是调用handleClose
        _socket->shutdownWrite();
    }
}

}   //kit_muduo
