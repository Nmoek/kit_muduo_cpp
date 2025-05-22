/**
 * @file channel.cpp
 * @brief
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 11:25:34
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/channel.h"
#include "net/net_log.h"
#include "net/event_loop.h"

#include <sys/epoll.h>

namespace kit_muduo {

const int32_t Channel::kNonEvent = 0;
const int32_t Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int32_t Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int32_t fd)
    :_loop(loop)
    ,_fd(fd)
    ,_events(0)
    ,_revents(0)
    ,_index(-1)
    ,_tied(false)
{

}

void Channel::handleEvent(TimeStamp receiveTime)
{
    if(_tied)
    {
        std::shared_ptr<void> p = _tie.lock();
        if(!p)
        {
            CHANNEL_WARN() << "Channel tie is null!" << std::endl;
            return;
        }
        handleEventWithGuard(receiveTime);
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}


void Channel::remove()
{
    _loop->removeChannel(this);
}


void Channel::update()
{
    _loop->updateChannel(this);
}

void Channel::handleEventWithGuard(TimeStamp receiveTime)
{
    int32_t revent = _revents;
    CHANNEL_F_INFO("Channel: trigger event 0X%x \n", revent);
    // 1. 先处理关闭
    if((revent & EPOLLHUP)
        && !(revent & EPOLLIN))
    {
        CHANNEL_INFO() << "Channel: trigger close event" << std::endl;
        if(_closeCallback)
            _closeCallback();
    }

    // 2. 继续处理错误事件
    if(revent & EPOLLERR)
    {
        CHANNEL_INFO() << "Channel: trigger error event" << std::endl;
        if(_errorCallback)
            _errorCallback();
    }

    // 3. 继续处理可读事件
    if(revent & (EPOLLIN | EPOLLRDNORM | EPOLLPRI | EPOLLRDHUP))
    {
        CHANNEL_INFO() << "Channel: trigger read event" << std::endl;
        if(_readCallback)
            _readCallback(receiveTime);
    }

    // 3. 最后处理可写事件
    if(revent & (EPOLLOUT | EPOLLWRNORM))
    {
        CHANNEL_INFO() << "Channel: trigger write event" << std::endl;
        if(_writeCallback)
            _writeCallback();
    }
}

}   //kit_muduo