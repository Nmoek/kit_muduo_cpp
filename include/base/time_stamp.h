/**
 * @file time_stamp.h
 * @brief 通用时间类
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-20 21:31:27
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_TIME_STAMP_H__
#define __KIT_TIME_STAMP_H__

#include <cstdint>
#include <string>
#include <optional>

namespace kit_muduo {


class TimeStamp
{
public:


    /**
     * @brief 普通构造
     * @param[in] millSeconds
     */
    explicit TimeStamp(int64_t epoch_ms = 0);

    /**
     * @brief 转为UTC RFC3339标准时间字符串 
     * @return std::string 
     */
    std::string toUtcRfc3339() const;
    /**
     * @brief 日志使用本地机器时间字符串(后续会统一为utc)
     * @param format 
     * @return std::string 
     */
    std::string toLogString(const std::string &format = "%Y-%m-%d %H:%M:%S") const;


    /**
     * @brief 获取时间 单位ms
     * @return int64_t
     */
    int64_t millSeconds() const { return real_time_ms_; }

    /**
     * @brief 获取时间 单位s
     * @return uint64_t 
     */
    int64_t seconds() const { return real_time_ms_ / 1000; }

    TimeStamp& addTime(int64_t millseconds);
    TimeStamp& subTime(int64_t millseconds);
    TimeStamp& addTime(const TimeStamp &t);
    TimeStamp& subTime(const TimeStamp &t);

    bool operator==(const TimeStamp &t) const
    {
        return real_time_ms_ == t.real_time_ms_;
    }

    bool operator<(const TimeStamp &t) const
    {
        return real_time_ms_ < t.real_time_ms_;
    }

    bool operator>(const TimeStamp &t) const
    {
        return real_time_ms_ > t.real_time_ms_;
    }

    bool operator>=(const TimeStamp &t) const
    {
        return real_time_ms_ >= t.real_time_ms_;
    }


    bool operator<=(const TimeStamp &t) const
    {
        return real_time_ms_ <= t.real_time_ms_;
    }

public:
    static TimeStamp Now();

    static int64_t NowMs();

    static int64_t NowUs();

    static int64_t MonotonicNowMs();

    static int64_t MonotonicNowS();

    static std::string FormatLogTimeStamp(int64_t epoch_ms, const std::string &format);

    static std::optional<TimeStamp> ParseRfc3339(std::string value);

private:
    /// @brief 时间戳ms
    int64_t real_time_ms_{0};
};



}   // kit_muduo
#endif