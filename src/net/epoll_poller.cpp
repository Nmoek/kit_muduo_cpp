/**
 * @file epoll_poller.cpp
 * @brief epoll实现的IO复用组件
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 18:41:28
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "net/epoll_poller.h"
#include "net/net_log.h"
#include "net/channel.h"

#include <string.h>
#include <unistd.h>
#include <assert.h>

namespace kit_muduo {

int32_t EpollPoller::kInitEventNums = 16;

EpollPoller::EpollPoller(EventLoop *loop)
    :Poller(loop)
    ,_epollfd(::epoll_create1(EPOLL_CLOEXEC))
    ,_events(kInitEventNums)
{
    if(_epollfd < 0)
    {
        POLLER_F_FATAL("epoll create error! %d:%s \n", errno, strerror(errno));
        abort();
    }
}

EpollPoller::~EpollPoller()
{
    if(_epollfd > 0)
        ::close(_epollfd);
}

TimeStamp EpollPoller::poll(int32_t timeout, ChannelList *channelList)
{
    TimeStamp now = TimeStamp::Now();
    int32_t numEvents = ::epoll_wait(_epollfd, _events.data(), _events.size(), timeout);
    int32_t cur_errno = errno;
    if(numEvents < 0)
    {
        POLLER_F_ERROR("epoll_wait error! %d:%s \n", cur_errno, strerror(cur_errno));
        return TimeStamp::Now();
    }
    else if(0 == numEvents)
    {
        POLLER_F_WARN("epoll_wait timeout!\n");
        return now;
    }
    POLLER_F_DEBUG("%d event trigger \n", numEvents);
    fillActiveEvent(numEvents, channelList);
    if(numEvents == _events.size())
    {
        _events.resize(numEvents * 2);
    }
    POLLER_F_DEBUG("%d event trigger \n", numEvents);

    return now;
}

void EpollPoller::updateChannel(Channel *channel)
{
    // 表示当前传入Channel的状态
    int32_t status = channel->index();
    int32_t fd = channel->fd();
    if(Poller::kNew == status || Poller::kDeleted == status) // epoll中未添加
    {
        if(Poller::kNew == status)
        {
            assert(_channels.find(fd) == _channels.end());
            _channels[fd] = channel;
        }
        else //曾添加过 已从epoll中删除 _channels中还存在
        {
            assert(_channels.find(fd) != _channels.end());
            assert(_channels[fd] == channel);
        }
        // kNew ==> kAdded
        // kDeleted ==> kAdded
        channel->setIndex(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        if(channel->isNonEvent()) // 没有任何事件需要监听 从epoll中删除 _channels中还存在
        {
            channel->setIndex(Poller::kDeleted);
            update(EPOLL_CTL_DEL, channel);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::removeChannel(Channel *channel)
{
    int32_t fd = channel->fd();
    int32_t status = channel->index();

    // 从_channels删除
    size_t n = _channels.erase(fd);
    assert(n == 1);
    if(Poller::kAdded == status) // epoll中还存在 同时从epoll中删除
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->setIndex(kNew);
}

void EpollPoller::update(int32_t operation, Channel *channel)
{
    struct epoll_event ev = {0};
    int32_t fd = channel->fd();
    int32_t events = channel->events();
    ev.events = events;
    ev.data.ptr = channel;
    int32_t res = ::epoll_ctl(_epollfd, operation, fd, &ev);
    if(res < 0)
    {
        POLLER_F_ERROR("fd[%d] events[0X%x] epoll_ctl error! %d:%s \n", fd, events, errno, strerror(errno));
        return;
    }
    return;
}

void EpollPoller::fillActiveEvent(int32_t numEvents, ChannelList *channelList)
{
    for(int i = 0;i < numEvents;++i)
    {
        Channel *c = static_cast<Channel*>(_events[i].data.ptr);
        if(!c)
        {
            POLLER_F_ERROR("i:%d, %p void* --> Channel* error! \n", i, _events[i].data.ptr);
            continue;
        }

        POLLER_F_DEBUG("===> fd[%d] events[0x%x] active! \n", c->fd(), _events[i].events);
        // 获取当前真正发生的事件
        c->setRevents(_events[i].events);
        channelList->push_back(c);
    }
}


}   //kit_muduo