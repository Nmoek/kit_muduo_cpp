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

std::atomic_int Timer::s_createNum{0};

Timer::Timer(const TimerCb &cb, TimeStamp when, int64_t interval)
    :_timerCallback(std::move(cb))
    ,_expiration(when)
    ,_interval(interval)
    ,_repeated(interval > 0)
    ,_sequence(s_createNum++)
{

}

void Timer::restart(TimeStamp now)
{
    if(_repeated)
    {
        _expiration = TimeStamp::AddTime(now, _interval);
    }
    else
    {
        _expiration = TimeStamp();
    }
}


}   // kit_muduo