/**
 * @file event_loop.h
 * @brief 事件循环对外调用类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-21 01:46:42
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_EVENT_LOOP_H__
#define __KIT_EVENT_LOOP_H__

#include "base/noncopyable.h"
#include "base/time_stamp.h"
#include "base/util.h"
#include "net/call_backs.h"

#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>

namespace kit_muduo {

class Channel;
class Poller;
class Timer;
class SampleTimerQueue;

class EventLoop: Noncopyable
{
public:
    using Func = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();

    void quit();

    void wakeup();

    TimeStamp pollReturnTime() const { return _pollReturnTime; }

    /**
     * @brief 在当前loop中执行回调函数
     * @param[in] cb
     */
    void runInLoop(Func cb);

    /**
     * @brief 把回调函数放入到队列中 唤醒loop所在线程再执行回调函数
     * @param[in] cb
     */
    void queueInLoop(Func cb);

    /****定时器 ****/

    /**
     * @brief 在某个时间点执行任务
     * @param[in] time  时间点
     * @param[in] cb  任务
     * @return std::shared_ptr<Timer>
     */
    std::shared_ptr<Timer> runAt(TimeStamp time, TimerCb cb);
    /**
     * @brief 等待一定时间后执行任务
     * @param[in] delay 等待时长 单位s
     * @param[in] cb 任务
     * @return std::shared_ptr<Timer>
     */
    std::shared_ptr<Timer> runAfter(int64_t delay, TimerCb cb);
    /**
     * @brief 每间隔一段时间执行一次任务
     * @param[in] interval 循环间隔 单位s
     * @param[in] cb 任务
     * @return std::shared_ptr<Timer>
     */
    std::shared_ptr<Timer> runEvery(int64_t interval, TimerCb cb);
    /**
     * @brief 取消定时器
     * @param[in] timer
     */
    void cancel(std::shared_ptr<Timer> timer);

    /****事件增删***/
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    bool isInLoopThread() const { return _threadId == GetThreadPid(); }

private:
    /**
     * @brief 处理wakeup读事件
     */
    void handleRead();
    /**
     * @brief 开始执行所有事件回调
     */
    void doPendingFuncs();

private:
    using ChannelList = std::vector<Channel*>;

    /// @brief 是否正在执行事件循环
    std::atomic_bool _looping;
    /// @brief 是否退出事件循环
    std::atomic_bool _quit;
    /// @brief 当前是否有事件回调正在执行
    std::atomic_bool _callingPendingFunc;
    /// @brief 获取到活跃事件集的时间
    TimeStamp _pollReturnTime;
    /// @brief 当前事件循环所属线程内核PID
    const pid_t _threadId;
    /// @brief IO复用组件
    std::unique_ptr<Poller> _poller;
    /// @brief 简易定时器队列
    std::unique_ptr<SampleTimerQueue> _timerQueue;


    /// @brief 当前活跃的事件集合
    ChannelList _activeChannels;
    /// @brief 当前活跃的某个事件
    Channel* _curActiveChannel;

    /// @brief 唤醒subReactor组件 eventfd
    int32_t _wakeupFd;
    std::unique_ptr<Channel> _wakeupChannel;

    /// @brief 待执行事件回调函数
    std::vector<Func> _pendingFuncs;
    /// @brief 保护回调函数集合锁
    mutable std::mutex _mutex;

};


}   // kit_muduo
#endif