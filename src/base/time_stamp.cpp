/**
 * @file time_stamp.cpp
 * @brief 通用时间类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 21:37:12
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/time_stamp.h"

#include <sys/time.h>

namespace kit_muduo {

TimeStamp::TimeStamp(uint64_t millSeconds)
    :_millSeconds(millSeconds)
{

}

std::string TimeStamp::toString()
{
    char buf[128] = {0};
    time_t now = _millSeconds / 1000;
    struct tm *tm = localtime(&now);
    ::strftime(buf, 128, "%Y-%m-%d %H:%M:%S", tm);
    return buf;
}


TimeStamp TimeStamp::Now()
{
    struct timeval t = {0};
    gettimeofday(&t, nullptr);
    return TimeStamp(t.tv_sec * 1000 + t.tv_usec / 1000);
}

uint64_t TimeStamp::NowTimeStamp()
{
    struct timeval t = {0};
    gettimeofday(&t, nullptr);
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}




}   //kit_muoduo