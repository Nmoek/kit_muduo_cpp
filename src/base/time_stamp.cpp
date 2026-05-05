/**
 * @file time_stamp.cpp
 * @brief 通用时间类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 21:37:12
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/time_stamp.h"

#include <ctime>
#include <sys/time.h>

namespace kit_muduo {

TimeStamp::TimeStamp(uint64_t millSeconds)
    :real_time_ms_(millSeconds)
{

}

std::string TimeStamp::toString() const
{
    char buf[128] = {0};
    time_t now = real_time_ms_ + 8*60*60;
    time_t remain = now % 1000;
    now /= 1000;
    struct tm tm;
    tm = *localtime_r(&now, &tm);
    ::strftime(buf, 128, "%Y-%m-%d %H:%M:%S", &tm);
    std::string res(buf);
    res += ".";
    res += std::to_string(remain);

    return res;
}

void TimeStamp::fromString(const std::string &dataStr)
{
    struct tm t = {0};
    ::strptime(dataStr.c_str(), "%Y-%m-%d %H:%M:%S", &t);
    real_time_ms_ = static_cast<uint64_t>(mktime(&t)) * 1000;
}

int64_t TimeStamp::toMonotonic() const 
{ 
    return GetMonotonicMS() - (NowMs() - real_time_ms_); 
}

TimeStamp& TimeStamp::addTime(int64_t millseconds)
{
    real_time_ms_ += millseconds;
    return *this;
}

TimeStamp& TimeStamp::subTime(int64_t millseconds)
{
    real_time_ms_ -= millseconds;
    return *this;
}

TimeStamp& TimeStamp::addTime(const TimeStamp &t)
{
    real_time_ms_ += t.millSeconds();
    return *this;
}

TimeStamp& TimeStamp::subTime(const TimeStamp &t)
{
    real_time_ms_ -= t.millSeconds();
    return *this;
}

TimeStamp TimeStamp::Now()
{
    return TimeStamp(NowMs());
}

int64_t TimeStamp::NowMs()
{
    struct timeval t = {0};
    gettimeofday(&t, nullptr);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}


time_t TimeStamp::Str2TimeStamp(const std::string &timeStr)
{
    struct tm t = {0};
    ::strptime(timeStr.c_str(), "%Y-%m-%d %H:%M:%S", &t);
    return mktime(&t);
}


std::string TimeStamp::TimeStamp2Str(time_t timeStamp)
{
    char buf[128] = {0};
    struct tm tm;
    tm = *localtime_r(&timeStamp, &tm);
    ::strftime(buf, 128, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}
    

TimeStamp TimeStamp::FromMonotonic(int64_t ms)
{
    return TimeStamp(NowMs() - (GetMonotonicMS()- ms));
}




}   // kit_muoduo