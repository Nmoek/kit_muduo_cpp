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

#include "base/util.h"
#include <bits/stdint-uintn.h>
#include <string>

namespace kit_muduo {


class TimeStamp
{
public:
    /**
     * @brief 默认构造
     */
    explicit TimeStamp() = default;

    /**
     * @brief 普通构造
     * @param[in] millSeconds
     */
    explicit TimeStamp(uint64_t millSeconds);

    /**
     * @brief 转为时间字符串, 如2025-05-20 22:14:17
     * @return std::string
     */
    std::string toString() const;

    /**
     * @brief 时间字符串转为时间戳(仅支持格式%Y-%m-%d %H:%M:%S)
     * @param dataStr 
     * @return uint64_t 
     */
    void fromString(const std::string &dataStr);

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

    /**
     * @brief 获取已存时间戳对应的单调递增时间值
     * @return uint64_t 
     */
    int64_t toMonotonic() const;

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
    /**
     * @brief  生成时间戳对象
     * @return TimeStamp
     */
    static TimeStamp Now();
    /**
     * @brief 获取当前时间戳ms
     * @return uint64_t
     */
    static int64_t NowMs();

    /**
     * @brief 时间字符串 --> 时间戳
     * @param timeStr 
     * @return std::string 
     */
    static time_t Str2TimeStamp(const std::string &timeStr);

    /**
     * @brief 时间戳 --> 时间字符串
     * @param timeStamp 
     * @return std::string 
     */
    static std::string TimeStamp2Str(time_t timeStamp);


    static TimeStamp FromMonotonic(int64_t ms);

private:
    /// @brief 时间戳ms
    int64_t real_time_ms_{0};
};



}   // kit_muduo
#endif