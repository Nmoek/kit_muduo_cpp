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

namespace kit_muduo {


class PollPoller: public Poller
{
public:
    PollPoller(EventLoop *loop) :Poller(loop) { }

    /**
     * @brief 等价于epoll_wait 执行事件循环
     * @param[in] timeout 期望超时时间
     * @param[out] channelList 触发事件后的Channel集合
     * @return TimeStamp
     */
    virtual TimeStamp poll(int32_t timeout, ChannelList *channelList) { return TimeStamp(); }

     /**
      * @brief 向Reacto中添加事件
      * @param[in] channel
      */
    virtual void updateChannel(Channel *channel) { }

     /**
      * @brief 从Reacto中删除事件
      * @param[in] channel
      */
    virtual void removeChannel(Channel *channel) { }
};
}   //kit_muduo
#endif