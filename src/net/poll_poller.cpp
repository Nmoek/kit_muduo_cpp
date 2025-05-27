/**
 * @file poll_poller.cpp
 * @brief poll实现的IO复用组件
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 14:42:48
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/poll_poller.h"
#include "net/net_log.h"
#include "net/channel.h"

#include <assert.h>

namespace kit_muduo {

PollPoller::PollPoller(EventLoop *loop)
    :Poller(loop)
{

}

/**
* @brief 等价于epoll_wait 执行事件循环
* @param[in] timeout 期望超时时间
* @param[out] channelList 触发事件后的Channel集合
* @return TimeStamp
*/
TimeStamp PollPoller::poll(int32_t timeout, ChannelList *channelList)
{
    TimeStamp now = TimeStamp::Now();

    int32_t res = ::poll(_eventList.data(), _eventList.size(), timeout);
    int32_t cur_errno = errno;
    if(res < 0)
    {
        POLLER_F_ERROR("poll error! %d:%s \n", cur_errno, strerror(cur_errno));
        return TimeStamp::Now();
    }
    else if(0 == res)
    {
        POLLER_F_WARN("poll timeout!\n");
        return now;
    }
    fillActiveEvent(channelList);
    // 不需要扩容，每一次poll返回的都是全部fd

    return now;
}

/**
* @brief 向Reacto中添加事件
* @param[in] channel
*/
void PollPoller::updateChannel(Channel *channel)
{
    int32_t state = channel->index();
    int32_t fd = channel->fd();
    if(Poller::kNew == state) // 在Poller中不存在
    {
        _channels[fd] = channel; //Poller中添加
        channel->setIndex(Poller::kAdded);
        // poll中添加
        update(channel);

    }
    else if(Poller::kDeleted == state) // 未监听任何事件但仍存在于Poller记录中
    {
        assert(_channels.find(fd) != _channels.end());
        assert(_channels[fd] == channel);

        channel->setIndex(Poller::kAdded);
        update(channel);
    }
    else  // 当前有监听事件也存在于Poller记录中  可能需要修改事件
    {
        if(channel->isNonEvent())
            channel->setIndex(kDeleted);

        //poll中修改/删除
        update(channel);
    }
}

/**
* @brief 从Reacto中删除事件
* @param[in] channel
*/
void PollPoller::removeChannel(Channel *channel)
{
    int32_t fd = channel->fd();
    int32_t state = channel->index();

    size_t n = _channels.erase(fd);
    assert(n == 1);
    if(Poller::kAdded == state)
    {
        channel->setIndex(Poller::kDeleted);
        update(channel);
    }

    channel->setIndex(Poller::kNew);
}


void PollPoller::fillActiveEvent(ChannelList *channelList)
{
    for(int i = 0;i < _eventList.size();++i)
    {
        if(0 == _eventList[i].revents)
            continue;
        Channel *c = Poller::_channels[_eventList[i].fd];

        POLLER_F_DEBUG("PollPoller::fillActiveEvent fd[%d] %p \n", _eventList[i].fd, c);

        c->setRevents(_eventList[i].revents);
        channelList->push_back(c);
    }
    return;
}

void PollPoller::update(Channel *channel)
{
    int32_t fd = channel->fd();
    int32_t state = channel->index();

    auto it = std::find_if(_eventList.begin(), _eventList.end(), [fd](struct pollfd p){
        return fd == p.fd;
    });

    if(it == _eventList.end())
    {
        // 增加监听的fd
        if(kAdded == state)
        {
            struct pollfd p;
            p.fd = fd;
            p.events = channel->events();
            _eventList.push_back(p);
            POLLER_F_DEBUG("poll add success! fd[%d]!\n", fd);
        }
        else
        {
            POLLER_F_ERROR("poll not find fd[%d]!\n", fd);

        }
        return;
    }
    assert(it != _eventList.end());
    assert(fd == it->fd);

    // 删除监听的fd
    if(Poller::kDeleted == state)
    {
        _eventList.erase(it);
        POLLER_F_DEBUG("poll del fd[%d]\n", fd);
        return;
    }

    // 修改监听事件
    POLLER_F_DEBUG("poll update fd[%d], ev[0x%x] -> [0x%x]\n", fd,  it->events, channel->events());

    it->fd = fd;
    it->events = channel->events();

    return;
}





}   //kit_muduo