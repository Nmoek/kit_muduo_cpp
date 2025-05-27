/**
 * @file poll_poller.h
 * @brief poll实现的IO复用组件
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 16:21:06
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_POLL_POLLER_H__
#define __KIT_POLL_POLLER_H__

#include "base/noncopyable.h"
#include "net/poller.h"
#include "base/time_stamp.h"

#include <sys/poll.h>

namespace kit_muduo {


class PollPoller: public Poller
{
public:
    PollPoller(EventLoop *loop);

    ~PollPoller() override = default;

    /**
    * @brief 等价于epoll_wait 执行事件循环
    * @param[in] timeout 期望超时时间
    * @param[out] channelList 触发事件后的Channel集合
    * @return TimeStamp
    */
    TimeStamp poll(int32_t timeout, ChannelList *channelList) override;

    /**
    * @brief 向Reacto中添加事件
    * @param[in] channel
    */
    void updateChannel(Channel *channel) override;

    /**
    * @brief 从Reacto中删除事件
    * @param[in] channel
    */
    void removeChannel(Channel *channel) override;

private:
    void fillActiveEvent(ChannelList *channelList);

    void update(Channel *channel);

private:
    using EventList = std::vector<struct pollfd>;
    EventList _eventList;
};

}   //kit_muduo
#endif