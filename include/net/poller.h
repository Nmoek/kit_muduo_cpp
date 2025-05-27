/**
 * @file poller.h
 * @brief IO复用组件类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 16:03:37
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_POLLER_H__
#define __KIT_POLLER_H__

#include "base/noncopyable.h"

#include <unordered_map>
#include <vector>

namespace kit_muduo {

class EventLoop;
class Channel;
class TimeStamp;

class Poller: Noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    /**
     * @brief 等价于epoll_wait 执行事件循环
     * @param[in] timeout 期望超时时间
     * @param[out] channelList 触发事件后的Channel集合
     * @return TimeStamp
     */
    virtual TimeStamp poll(int32_t timeout, ChannelList *channelList) = 0;

    /**
     * @brief 向Reacto中添加事件
     * @param[in] channel
     */
    virtual void updateChannel(Channel *channel) = 0;

    /**
     * @brief 从Reacto中删除事件
     * @param[in] channel
     */
    virtual void removeChannel(Channel *channel) = 0;

    /**
     * @brief channel是否存在
     * @param[in] channel
     * @return true
     * @return false
     */
    virtual bool hasChannel(Channel *channel);

public:
    static Poller* NewDefaultPoller(EventLoop *loop);

public:
    /******Channel状态机 表示的是channel在IO复用组件中的状态, 不是IO复用组件的实际状态******/
    /// Channel未添加到Poller
    static const int32_t kNew = -1;
    /// Channel已添加到Poller
    static const int32_t kAdded = 1;
    /// Channel从Poller删除
    static const int32_t kDeleted = 2;
protected:
    /**
     * @brief key: socket fd ----> val: Channel*对象
     */
    using ChannelMap = std::unordered_map<int32_t, Channel*>;
    ChannelMap _channels;
private:
    EventLoop *_ownerLoop;
};


}   // kit_muduo
#endif