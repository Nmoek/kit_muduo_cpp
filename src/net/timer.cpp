/**
 * @file timer.cpp
 * @brief 定时器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-27 21:25:00
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "net/timer.h"

namespace kit_muduo {

std::atomic_int Timer::s_createNum{1};

Timer::Timer(TimerCb cb, int64_t when, int64_t interval)
    :timer_callback_(cb ? std::move(cb) : TimerCb())
    ,expiration_(when)
    ,interval_(interval)
    ,repeated_(interval > 0)
    ,sequence_(s_createNum++)
{

}

void Timer::restart(int64_t now)
{
    if(repeated_)
    {
        expiration_ = now + interval_;
    }
    else
    {
        expiration_ = 0;
    }
}



}   // kit_muduo