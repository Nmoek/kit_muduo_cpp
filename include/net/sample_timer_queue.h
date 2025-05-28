/**
 * @file sample_timer_queue.h
 * @brief 简易定时器队列
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 22:00:09
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_SAMPLE_TIMRE_QUEUE_H__
#define __KIT_SAMPLE_TIMRE_QUEUE_H__

#include "base/noncopyable.h"
#include "net/call_backs.h"
#include "net/channel.h"
#include "net/timer.h"

#include <set>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <iostream>

namespace kit_muduo {

class EventLoop;
class Channel;
class TimeStamp;
class Timer;

/**
 * @brief  简易定时器队列，定时精度不高，尽可能触发
 */
class SampleTimerQueue: Noncopyable
{
public:
    SampleTimerQueue(EventLoop *loop);

    ~SampleTimerQueue();

    std::shared_ptr<Timer> addTimer(TimerCb cb, TimeStamp when, int64_t interval = 0);

    void cancel(std::shared_ptr<Timer> timer);

private:
    using WillExpiredTimer = std::pair<TimeStamp, std::shared_ptr<Timer>>;
    /**
     * @brief 定时器队列比较器
     */
    struct WillExpiredTimerComparer
    {
        // 不比较second的地址值
        bool operator()(const WillExpiredTimer &a, const WillExpiredTimer &b) const
        {
            if(!(a.first == b.first))
                return a.first < b.first;
            return a.second->sequence() < b.second->sequence();
        }
    };
    using WillExpiredTimerList = std::set<WillExpiredTimer, WillExpiredTimerComparer>;

    using ActiveTimerMap = std::unordered_map<int64_t, std::shared_ptr<Timer>>;


    void addTimerInLoop(std::shared_ptr<Timer> timer);
    void cancelInLoop(std::shared_ptr<Timer> timer);

    std::vector<WillExpiredTimer> getExpired(TimeStamp now);
    void handleRead();
    void reset(const std::vector<WillExpiredTimer>& expired, TimeStamp now);

    bool insert(std::shared_ptr<Timer> timer);

    void readTimerFd();
    void resetTimerFd(TimeStamp nextExpired);

private:
    /// @brief 所属事件循环
    EventLoop *_loop;
    /// @brief 定时器事件fd
    int32_t _timerFd;
    /// @brief 定时器事件Channel
    Channel _timerChannel;

    /// @brief 定时器有序集合, 按到期时间点排序
    WillExpiredTimerList _timers;

    /// @brief 记录正在计时状态的定时器
    ActiveTimerMap _activeTimers;
    /// @brief 是否正在执行定时器回调函数
    std::atomic_bool _callingExipiredTimers;
    /// @brief 记录取消状态的定时器
    ActiveTimerMap _cancelingTimers;


};


}   //kit_muoduo
#endif