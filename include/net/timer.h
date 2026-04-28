/**
 * @file timer.h
 * @brief 定时器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 21:18:15
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_TIMER_H__
#define __KIT_TIMER_H__

#include "base/noncopyable.h"
#include "base/time_stamp.h"
#include "net/call_backs.h"

#include <atomic>

namespace kit_muduo {


class Timer: Noncopyable
{
public:
    Timer(TimerCb cb, int64_t when, int64_t interval);

    ~Timer() = default;

    void run() { timer_callback_(); }

    int64_t expiration() const { return expiration_; }
    double interval() const { return interval_; }
    bool repeated() const { return repeated_; }
    // for compare 用于容器比较指定最大值，其他场景禁止使用
    void setSequence(int64_t s) { sequence_ = s; }
    int64_t sequence() const { return sequence_; };

    /**
     * @brief 重置定时器到期时间点
     * @param[in] now  当前时间点
     */
    void restart(int64_t now);

public:
    static int32_t createNum() { return s_createNum; }

private:
    static std::atomic_int s_createNum;

private:
    /// @brief 定时任务
    TimerCb timer_callback_;
    /// @brief 唤醒时间点
    int64_t expiration_;
    /// @brief 定时间隔 ms
    int64_t interval_;
    /// @brief 是否是循环定时器
    bool repeated_;
    /// @brief 定时器ID
    int64_t sequence_;

};
}   //kit_muduo


#endif