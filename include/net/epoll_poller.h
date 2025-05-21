/**
 * @file epoll_poller.h
 * @brief  epoll实现的IO复用组件
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 16:20:45
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_EPOLL_POLLER_H__
#define __KIT_EPOLL_POLLER_H__

#include "base/noncopyable.h"
#include "net/poller.h"
#include "base/time_stamp.h"

#include <sys/epoll.h>

namespace kit_muduo {


class EpollPoller: public Poller
{
public:

    EpollPoller(EventLoop *loop);

    ~EpollPoller() override;


    /**
    * @brief 等价于epoll_wait 执行事件循环
    * @param[in] timeout 期望超时时间
    * @param[out] channelList 触发事件后的Channel集合
    * @return TimeStamp
    */
    TimeStamp poll(int32_t timeout, ChannelList *channelList) override;

    /**
        注意：以下类似epoll_ctl的操作存在 代理关系
        Channel ====> EventLoop ====> Poller
     */
    /**
    * @brief 向Reactor中添加事件
    * @param[in] channel
    */
    void updateChannel(Channel *channel) override;

    /**
    * @brief 从Reactor中删除事件
    * @param[in] channel
    */
    void removeChannel(Channel *channel) override;

private:
    /**
     * @brief 填充当前的活跃连接
     * @param[in] numEvents
     * @param[in] channelList
     */
    void fillActiveEvent(int32_t numEvents, ChannelList *channelList);

    /**
     * @brief 真正更新事件 epoll_ctl add/mod/del
     * @param[in] operate
     * @param[in] channel
     */
    void update(int32_t operation, Channel *channel);

private:
    /// @brief 初始事件数量
    static int32_t kInitEventNums;

private:
    using EventList = std::vector<struct epoll_event>;

    /// @brief epoll句柄
    int32_t _epollfd;
    /// @brief epoll事件集合
    EventList _events;
};
}   //kit_muduo
#endif