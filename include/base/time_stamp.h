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
    std::string toString();

    /**
     * @brief 获取时间 单位ms
     * @return int64_t
     */
    int64_t millSeconds() const { return _millSeconds; }

    bool operator==(const TimeStamp &t) const
    {
        return _millSeconds == t._millSeconds;
    }

    bool operator<(const TimeStamp &t) const
    {
        return _millSeconds < t._millSeconds;
    }

    bool operator>(const TimeStamp &t) const
    {
        return _millSeconds > t._millSeconds;
    }

    bool operator>=(const TimeStamp &t) const
    {
        return _millSeconds >= t._millSeconds;
    }


    bool operator<=(const TimeStamp &t) const
    {
        return _millSeconds <= t._millSeconds;
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
    static uint64_t NowTimeStamp();

    /**
     * @brief 时刻计算, 当前时刻加上秒数后的时刻
     * @param[in] now
     * @param[in] seconds
     * @return TimeStamp
     */
    static TimeStamp AddTime(TimeStamp now, int64_t seconds);

private:
    /// @brief 时间戳ms
    uint64_t _millSeconds{0};
};



}   // kit_muduo
#endif