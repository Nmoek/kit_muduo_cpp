/**
 * @file log_level.cpp
 * @brief 日志级别
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-22 18:53:13
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/log_level.h"

using namespace kit_muduo;

std::string LogLevel::ToString(Level level)
{
    switch (level)
    {
    #define XX(level) \
        case level: return #level;

        XX(DEBUG);
        XX(INFO);
        XX(WARN);
        XX(ERROR);
        XX(FATAL);
    #undef XX
        default:
            return "UNKNOW";
    }
    return "UNKNOW";
}