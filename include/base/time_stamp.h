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
    TimeStamp() = default;
    TimeStamp(uint64_t millSeconds);

    std::string toString();

public:
    static TimeStamp Now();
    static uint64_t NowTimeStamp();

private:
    uint64_t _millSeconds{0};
};



}   // kit_muduo
#endif