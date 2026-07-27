/**
 * @file time_stamp.cpp
 * @brief 通用时间类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 21:37:12
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/time_stamp.h"
#include "date/date.h"

#include <charconv>
#include <chrono>
#include <ctime>
#include <exception>
#include <iomanip>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/time.h>

namespace kit_muduo {

namespace {



// std::string Timer2Str(time_t ts, const std::string& format)
// {
//     struct tm tm;
//     tm = *localtime_r(&ts, &tm);
//     char buf[100];
//     strftime(buf, sizeof(buf), format.c_str(), &tm);

//     return buf;
// }
template<typename Duration>
int64_t ReadClock(clockid_t clock_id)
{
    timespec spec{};
    if(::clock_gettime(clock_id, &spec) != 0)
    {
        return -1;
    }
    const auto s = std::chrono::seconds(spec.tv_sec);
    const auto ns = std::chrono::nanoseconds(spec.tv_nsec);
    return std::chrono::duration_cast<Duration>(s + ns).count();
}

int64_t MillisecondRemainder(int64_t epoch_ms)
{
    const int64_t remainder = epoch_ms % 1000;
    return remainder >= 0 ? remainder : remainder + 1000;
}


bool HasValidOffset(std::string_view value) 
{
    const auto is_digit = [](char c) 
    {
        return c >= '0' && c <= '9';
    };
    if(value.empty()) {
        return false;
    }
    if(value.back() == 'Z') 
    {
        return value.size() > 1;
    }
    if(value.size() < 6) 
    {
        return false;
    }
    const size_t pos = value.size() - 6;
    if((value[pos] != '+' && value[pos] != '-')
        || value[pos + 3] != ':') 
    {
        return false;
    }
    if(!is_digit(value[pos + 1]) || !is_digit(value[pos + 2])
        || !is_digit(value[pos + 4]) || !is_digit(value[pos + 5])) 
    {
        return false;
    }
    const int hours = (value[pos + 1] - '0') * 10 + value[pos + 2] - '0';
    const int minutes = (value[pos + 4] - '0') * 10 + value[pos + 5] - '0';
    return hours <= 23 && minutes <= 59;
}

bool HasValidFraction(std::string_view value) 
{
    const size_t dot = value.find('.', value.find('T'));
    if(dot == std::string_view::npos) 
    {
        return true;
    }
    size_t end = value.find_first_of("Z+-", dot + 1);
    if(end == std::string_view::npos || end <= dot + 1
        || end - dot - 1 > 3) 
        {
        return false;
    }
    for(size_t i = dot + 1; i < end; ++i) 
    {
        if(value[i] < '0' || value[i] > '9') 
        {
            return false;
        }
    }
    return true;
}


std::optional<date::sys_time
    <std::chrono::milliseconds>
> ParseUtc(const std::string &value)
{
    if(value.empty()
        || !HasValidOffset(value)
        || !HasValidFraction(value))
    {
        return std::nullopt;
    }

    std::string normalized{value};
    if(normalized.back() == 'Z')
    {
        normalized.replace(normalized.size() - 1, 1, "+00:00");
    }

    std::istringstream ss(normalized);
    date::sys_time<std::chrono::milliseconds> parsed{};
    ss >> date::parse("%FT%T%Ez", parsed);
    if(ss.fail() || ss.peek() != std::char_traits<char>::eof())
    {
        return std::nullopt;
    }
    return parsed;
}


}

TimeStamp::TimeStamp(int64_t epoch_ms)
    :real_time_ms_(epoch_ms)
{

}


std::string TimeStamp::toUtcRfc3339() const
{
    const date::sys_time<std::chrono::seconds> utc{
        date::floor<std::chrono::seconds>(std::chrono::milliseconds{real_time_ms_})
    };
    const auto remain_ms = MillisecondRemainder(real_time_ms_);
    std::ostringstream ss;
    ss << date::format("%FT%T", utc)
        << "." << std::setfill('0') << std::setw(3)
        << (remain_ms >= 0 ? remain_ms : 0) << "Z";
    return ss.str();
}

std::string TimeStamp::toLogString(const std::string &format) const
{
    const date::sys_time<std::chrono::seconds> utc{
        date::floor<std::chrono::seconds>(std::chrono::milliseconds{real_time_ms_})
    };
    // 手动将0时区往后调8h 给日志打印使用
    const auto fixed_utc = utc + std::chrono::hours{8};
    const int64_t remain = MillisecondRemainder(real_time_ms_);
    std::ostringstream ss;
    ss << date::format(format, fixed_utc)
        << "." << std::setfill('0') << std::setw(3)
        << remain;
    return ss.str();
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
    return ReadClock<std::chrono::milliseconds>(CLOCK_REALTIME);
}

int64_t TimeStamp::NowUs()
{
    return ReadClock<std::chrono::microseconds>(CLOCK_REALTIME);
}

int64_t TimeStamp::MonotonicNowMs()
{
    return ReadClock<std::chrono::milliseconds>(CLOCK_MONOTONIC);
}

int64_t TimeStamp::MonotonicNowS()
{
    return ReadClock<std::chrono::seconds>(CLOCK_MONOTONIC);
}

std::string TimeStamp::FormatLogTimeStamp(int64_t epoch_ms, const std::string &format)
{
    if(epoch_ms > std::numeric_limits<int64_t>::max()
        || epoch_ms < std::numeric_limits<int64_t>::min())
    {
        return {};
    }
    return TimeStamp(epoch_ms).toLogString(format);
}

std::optional<TimeStamp> TimeStamp::ParseRfc3339(std::string value)
{
    const auto &utc = ParseUtc(value);
    if(!utc.has_value())
    {
        return std::nullopt;
    }

    return TimeStamp(utc->time_since_epoch().count());
}


}   // kit_muoduo
