/**
 * @file sample_timer_queue.cpp
 * @brief 简易定时器队列
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 21:59:17
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/sample_timer_queue.h"
#include "net/net_log.h"
#include "net/socket.h"
#include "base/util.h"
#include "net/timer.h"
#include "net/event_loop.h"
#include "base/time_stamp.h"

#include <memory>
#include <sys/timerfd.h>
#include <unistd.h>
#include <assert.h>

namespace kit_muduo {

SampleTimerQueue::SampleTimerQueue(EventLoop *loop)
    :_loop(loop)
    ,_timerFd(CreateTimerFd())
    ,_timerChannel(loop, _timerFd)
    ,_callingExipiredTimers(false)
{
    _timerChannel.setReadCallback(std::bind(&SampleTimerQueue::handleRead, this));
    _timerChannel.enableReading();

    TIMER_F_DEBUG("SampleTimerQueue::timerFd[%d] \n", _timerFd);
}

SampleTimerQueue::~SampleTimerQueue()
{
    _timerChannel.disableAll();
    _timerChannel.remove();
    if(_timerFd > 0)
    {
        ::close(_timerFd);
        _timerFd = -1;
    }
}

std::shared_ptr<Timer> SampleTimerQueue::addTimer(TimerCb cb, TimeStamp when, int64_t interval)
{
    std::shared_ptr<Timer> timer(new Timer(std::move(cb), when.toMonotonic(), interval));


    _loop->runInLoop(std::bind(&SampleTimerQueue::addTimerInLoop, this, timer));

    return timer;
}

void SampleTimerQueue::cancel(std::shared_ptr<Timer> timer)
{
    if(nullptr == timer)
    {
        TIMER_F_ERROR("timer is null! \n");
        return;
    }


    _loop->runInLoop(std::bind(&SampleTimerQueue::cancelInLoop, this, timer));
}


void SampleTimerQueue::addTimerInLoop(std::shared_ptr<Timer> timer)
{
    bool earliest = insert(timer);

    // 当前插入的定时器已过期/ 队列为空 需要去更新timerfd上设置的时间
    if(earliest)
    {
        resetTimerFd(timer->expiration());
    }

}

void SampleTimerQueue::cancelInLoop(std::shared_ptr<Timer> timer)
{
    int64_t timer_id = timer->sequence();
    // 是否还处于计时状态
    auto ac_it = _activeTimers.find(timer_id);
    if(ac_it == _activeTimers.end())
    {
        // 不在计时有两种可能:
        // 1. 已经被取消过了
        // 2. 已经到时正在执行定时器任务
        if(_callingExipiredTimers)
        {
            assert(_cancelingTimers.find(timer_id) == _cancelingTimers.end()); //必然不可能存在于取消队列

            auto it3 = _timers.find(WillExpiredTimer(timer->expiration(), timer));

            // 增加一个递归定时器队列查询
            auto it4 = _runningTimers.find(timer_id);
            // 已经在执行定时器任务 则先放入取消队列延迟取消
            if(it3 != _timers.end() || it4 != _runningTimers.end())
            {
                _cancelingTimers.insert({timer->sequence(), timer});
                TIMER_INFO() << "timer: " << timer->sequence() << " will cancel!" << std::endl;
            }
            else  // 已经被取消 或 执行完毕
            {
                TIMER_WARN() << "timer: " << timer->sequence() << " has canceled/runing" << std::endl;
            }
        }
        return;
    }

    _timers.erase(WillExpiredTimer(ac_it->second->expiration(), ac_it->second));
    _activeTimers.erase(ac_it);
    TIMER_F_INFO("timer canel success! [%lld] \n", timer->sequence());
}

std::vector<SampleTimerQueue::WillExpiredTimer> SampleTimerQueue::getExpired(int64_t now)
{
    assert(_timers.size() == _activeTimers.size());
    std::vector<WillExpiredTimer> expired;

    if(_timers.empty())
    {
        TIMER_F_WARN("timers is empty!\n");
        return expired;
    }

    // 定时器事件触发却还没到达最小触发时间
    if(now < _timers.begin()->first)
    {
        TIMER_F_WARN("timerfd triggered early, now[%lld] < earliest[%lld]\n", now, _timers.begin()->first);
        return expired;
    }
    TimeStamp now_read_time = TimeStamp::FromMonotonic(now);
    TimeStamp earliest_read_time = TimeStamp::FromMonotonic(_timers.begin()->first);
    
    // 注意: 构造一个比较成员, 必须都是有值的
    /*
        pair对象 默认先比较first, TimeStamp需重载运算符operator<
        当first相同时，比较second,即shared_ptr<Timer>地址值
    */
    //等价于 pair{TimeStamp::now, Timer*::-1}
    std::shared_ptr<Timer> max_timer(std::make_shared<Timer>(nullptr, now, 0));

    max_timer->setSequence(INT64_MAX - 1);

    WillExpiredTimer tmp(now, max_timer);

    // 找到第一个不小于当前指定元素的迭代器位置
    // tmp <= end
    auto end = _timers.lower_bound(tmp);

    TIMER_F_DEBUG("now[%s][%lld] VS begin[%s][%lld] \n", now_read_time.toString().c_str(), now, earliest_read_time.toString().c_str(), _timers.begin()->first);


    // 取出所有到时的定时器
    std::copy(_timers.begin(), end, std::back_inserter(expired));
    _timers.erase(_timers.begin(), end);

    TIMER_F_DEBUG("find max timer, now[%s][%lld], expired.size[%d] \n",  now_read_time.toString().c_str(), now, expired.size());


    // 将已到期的定时器 从正在计时状态集合中删除
    // 复制一份到正在运行的定时器队列中 供递归查询
    _runningTimers.clear();
    for(auto &e : expired)
    {
        auto ac_it = _activeTimers.find(e.second->sequence());
        assert(ac_it != _activeTimers.end());
        _runningTimers.insert({ac_it->first, ac_it->second});
        _activeTimers.erase(ac_it);

    }

    assert(_timers.size() == _activeTimers.size());
    return expired;
}


void SampleTimerQueue::handleRead()
{
    readTimerFd();
    int64_t now = GetMonotonicMS();

    // 获取所有到时定时器
    std::vector<WillExpiredTimer> expired_timers = getExpired(now);

    _callingExipiredTimers = true;
    // 清空所有取消的定时器
    _cancelingTimers.clear();
    for(auto &it : expired_timers)
    {
        it.second->run();
    }
    _callingExipiredTimers = false;
    // 清空到时队列
    reset(expired_timers, now);
}

void SampleTimerQueue::reset(const std::vector<WillExpiredTimer>& expired, int64_t now)
{
    int64_t next_expired = 0;

    for(auto &e : expired)
    {
        auto it = _cancelingTimers.find( e.second->sequence());
        if(e.second->repeated()
            && it == _cancelingTimers.end()) // 循环定时器 且 此刻不属于取消状态
        {
            e.second->restart(now);
            insert(e.second);
        }
    }
    // 获取下一次超时时间
    if(!_timers.empty())
    {
        next_expired = _timers.begin()->second->expiration();
    }

    // 重置timer_fd
    if(next_expired > 0)
    {
        TIMER_F_DEBUG("nextExpired[%s][%lld] \n", TimeStamp::FromMonotonic(next_expired).toString().c_str(), next_expired);

        resetTimerFd(next_expired);
    }
}

bool SampleTimerQueue::insert(std::shared_ptr<Timer> timer)
{
    int64_t expired_time = timer->expiration();
    bool earliest = false;
    auto it = _timers.begin();

    // 这里要判断当前定时器是不是已经过期了
    if(it == _timers.end() || expired_time < it->first)
    {
        earliest = true;
    }
    //注意: 这里数量必须一致 否则状态会出问题
    _activeTimers.insert({timer->sequence(), timer});
    _timers.insert({expired_time, timer});

    TIMER_F_INFO("insert timer sucess! id[%d], expiredTime[%s][%lld], earliest[%d]\n", timer->sequence(), TimeStamp::FromMonotonic(expired_time).toString().c_str(), expired_time, earliest);


    return earliest;
}



void SampleTimerQueue::readTimerFd()
{
    int64_t howmay = 1;
    ssize_t n = ::read(_timerFd, &howmay, sizeof(howmay));

    int64_t now = GetMonotonicMS();
    TIMER_F_INFO("timer event trigger![%s][%lld] \n", TimeStamp::FromMonotonic(now).toString().c_str(), now);

    if(n != sizeof(howmay))
    {
        TIMER_F_ERROR("SampleTimerQueue::handleRead error! %d:%s \n", errno, strerror(errno));
    }
}

/**
 * @brief 将TimeStamp换算为timespec
 * @param nextExpired
 * @return struct timespec
 */
static struct timespec HowManyFromNow(int64_t next_expired)
{
    int64_t interval = next_expired - GetMonotonicMS();

    // 取决于硬件 一般现在都能达到微妙级
    // if(interval <= 100)
    //     interval = 100; // 精度最小只有100ms

    // 这里要主动钳制为一个最小正数
    if(interval <= 0)
    {
        interval = 1;
    }

    struct timespec res;
    res.tv_sec = static_cast<time_t>(interval / 1000);
    res.tv_nsec = static_cast<long>((interval % 1000) * 1000000);
    return res;
}

void SampleTimerQueue::resetTimerFd(int64_t next_expired)
{
    struct itimerspec old_value;
    struct itimerspec new_value;
    memset(&old_value, 0, sizeof(struct itimerspec));
    memset(&new_value, 0, sizeof(struct itimerspec));

    new_value.it_value = HowManyFromNow(next_expired);

    int32_t res = ::timerfd_settime(_timerFd, 0, &new_value, &old_value);
    if(res < 0)
    {
        TIMER_F_ERROR("::timerfd_settime error ! %d:%s \n", errno, strerror(errno));
        return;
    }
    TIMER_F_DEBUG("resetTimerFd success! nextTime=%ld\n", next_expired);
}

}   //kit_muduo