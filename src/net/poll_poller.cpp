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
    int32_t idx = channel->index(); // epoll中是状态,poll中是下标
    int32_t fd = channel->fd();
    if(idx < 0)    // 从没添加到poll中
    {
        struct pollfd p;
        p.fd = fd;
        p.events = static_cast<short>(channel->events());
        _eventList.push_back(p);
        idx = _eventList.size() - 1;
        channel->setIndex(idx);
        _channels[fd] = channel;
        POLLER_F_DEBUG("poll add success! fd[%d], idx[%d]!\n", fd, idx);

    }
    else // 存在Poller中 修改事件
    {
        struct pollfd *p = &_eventList[idx];
        // 修改监听事件
        POLLER_F_DEBUG("poll update fd[%d], idx[%d], ev[0x%x] -> [0x%x]\n", fd, idx, p->events, channel->events());

        p->fd = fd;
        p->events = static_cast<short>(channel->events());
        // 如果不监听任何事件 poll忽略该套接字
        if(channel->isNonEvent())
            p->fd = -fd-1;  //保证永远<0
    }

}

/**
* @brief 从Reacto中删除事件
* @param[in] channel
*/
void PollPoller::removeChannel(Channel *channel)
{
    int32_t fd = channel->fd();
    int32_t idx = channel->index();
    int32_t exchange_fd = _eventList.back().fd;

    size_t n = _channels.erase(fd);
    assert(n == 1);
    assert(idx >= 0 && idx < _eventList.size());
    // 不能直接删  会引起大量数据拷贝和移动
    // _eventList.erase(_eventList.begin() + idx);
    if(idx == _eventList.size() - 1)
    {
        _eventList.pop_back();
    }
    else
    {
        // 优化将目标pollfd交换到末尾，最后删除末尾
        std::swap(_eventList[idx], _eventList.back());
        _eventList.pop_back();

        // 更新数组索引
        Channel *swapChannel = _channels[exchange_fd];
        swapChannel->setIndex(idx);
    }

    POLLER_F_DEBUG("poll del swap fd[%d], idx[%d] <-- back fd[%d]\n", fd, idx, exchange_fd);

    return;
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

}   //kit_muduo